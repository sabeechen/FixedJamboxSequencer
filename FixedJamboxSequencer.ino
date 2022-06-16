//Block Step Sequencer Demo
//For use on JamBox (HackerBox #0028)
//  Each of 8 buttons turns on or off a note for the current beat
//  5th knob raises and lowers tempo
//  4th knob controls volume
//  3rd knob shifts toward a square wav -- go retro!
//  2nd knob adjsuts pitch, makes things weird.
//  1st knob does nothing.
//
// This was originally adapted from the Jambox instructable at 
// https://www.instructables.com/id/HACKERBOX-0028-JamBox/, however
// it now bears little resemblance to the original.  This fixes a 
// bunch of issues present in the original example, which would just 
// produce varrying samples of garbled audio from the PCM chip
//  (*) The DAC expects its samples to be LSB
//  (*) The pre-buffered waveforms introduced weird noise at the
//      end of each cycle because they didn't end exactly at the
//      beginning/end of a complete cylce.
//  (*) Buffer timing issues caused the DAC to run out of bytes to 
//      stream before they could be recomputed.
//  (*) The original scale wasn't precisely tuned, which made chords
//      sound very disharmonius.

#include "LedMatrix.h"
#include "driver/i2s.h"

// defines the pins used for the LED grids
#define CS_PIN 15
#define CLK_PIN 14
#define MISO_PIN 2 //Not Used
#define MOSI_PIN 12

// The number of 8x8 LED grids
#define NUMBER_OF_GRIDS 4

// The number of distinct notes, which is also the size of each LED grid.
#define NOTE_COUNT 8

// The sample rate we send to the I2C bus.  Higher numbers may make it 
// hard for the arduino to keep up and lead to distortion.  Since we
// only send clean sin() waves the low sample rate isn't very 
// important.
#define SAMPLE_RATE_HZ  8000

// The number of columns in the sequencer
#define SEQUENCER_COLUMN_COUNT (NUMBER_OF_GRIDS * NOTE_COUNT)

// Defines which potentiometers control which functions
#define POTENTIOMETER_COUNT 5
#define POTENTIOMETER_TEMPO 4 // Adjust the sequencer tempo
#define POTENTIOMETER_VOLUME 3 // Adjust the volume (gain)
#define POTENTIOMETER_SQUARE 2 // Transitions sinnal between a sin and square wave
#define POTENTIOMETER_PITCH 1 // Adjusts pitch up and down an octave

// The length of the buffer we push to the DAC on each loop.
#define BUFFER_LENGTH 60

// Which I2S the DAC is on.
#define I2S_NUM ((i2s_port_t)0)

// Helper constants.
#define PI2 (2*PI)

// Helper value that brings the scale [0, 1] to the range 
// expected for a 16 bit audio sample, experimentally adjusted to 
// avoid clipping.
#define SAMPLE_SCALE 4915.125

// Defines which pins have buttons and potentiometers.
static const int buttonPins[NOTE_COUNT] = {4, 5, 16, 17, 18, 19, 21, 23};
static const int potentiometerPins[POTENTIOMETER_COUNT] = {32, 33, 34, 35, 36};

// The frequency in Hz for each note.  This is C4, D4, E4....C5 with C5 at 444Hz. 
static const float noteFrequencies[NOTE_COUNT] = {264, 296.33, 332.62, 352.4, 395.56, 444, 498.37, 528.01};

static const i2s_config_t I2S_CONFIG = {
     // configures the ESP32 to act as an I2S master.
     .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),

     // Sets the sample rate.
     .sample_rate = SAMPLE_RATE_HZ,

     // Sets the sampel rate, two channels of 16 bits each.
     .bits_per_sample = (i2s_bits_per_sample_t) 16,
     .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,

     // Sets the I2s format.  The DAC expects LSB.
     .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S | I2S_COMM_FORMAT_STAND_MSB),

     // Not sure what these do.  Docs weren't much help ¯\_(ツ)_/¯
     .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
     .dma_buf_count = 6,
     .dma_buf_len = BUFFER_LENGTH,
     .use_apll = false
};

// Configures which pins we're using for I2S.
static const i2s_pin_config_t I2S_PIN_CONFIG = {
    .bck_io_num = 26,
    .ws_io_num = 25,
    .data_out_num = 22,
    .data_in_num = I2S_PIN_NO_CHANGE
};

LedMatrix ledMatrix = LedMatrix(NUMBER_OF_GRIDS, CLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
int currentColumn=0;

// Defines how much each note channel gets incremented for each sample.  Precomputed in setup().
volatile float frequencySteps[NOTE_COUNT] = {0};
volatile float frequencyCurrent[NOTE_COUNT] = {0};

// Determines overal gain, gets adjusted by a pot.
volatile double volume = 0;

// Determines how much the waveform produced resembles a square wave instead of being sinusoidal.
volatile double squareWaveInfluence = 0;

// Determines a shift in pitch.
volatile double pitchShift = 0;

// Stores the analog reading from each pot.
volatile int pots[5];

// Stores the state of each button.
volatile bool buttonState[NOTE_COUNT] = {0};

// Stores which notes form the sequencer should play.
bool gridState[SEQUENCER_COLUMN_COUNT][NOTE_COUNT] = {false};

// Determiens how many ms each "beat" of the synthesizer lasts.
int tempoDurationMs = 0;

// Tracks the ms at which the last beat started.
int lastAdvance = 0;

void setup() {
  Serial.begin(115200);

  // Start the I2S driver, which is needed to comunicate the the DAC.
  i2s_driver_install(I2S_NUM, &I2S_CONFIG, 0, NULL);
  i2s_set_pin(I2S_NUM, &I2S_PIN_CONFIG);
  i2s_set_sample_rates(I2S_NUM, SAMPLE_RATE_HZ);

  // Configure the pinmode for the buttons and pots.
  analogReadResolution(10);
  for (int i = 0 ; i < NOTE_COUNT; i++) {
    pinMode(buttonPins[i], INPUT_PULLDOWN);

    // Listen for button events on an interrupt.
    attachInterrupt(digitalPinToInterrupt(buttonPins[i]), buttonInterrupt, RISING);
  }
  for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
    pinMode(potentiometerPins[i], INPUT);
  }

  // Scroll "Jambox" across the screen.
  ledMatrix.init();
  scrollJamboxText();

  // For each of the notes, precompute how much to advance its phase for each sample.
  // If "f" is frequency in Hz and "t" is time in seconds, then sin(t * f * 2 * PI)
  // describes a wave of frequency f.  Since each sample jumps 1/SAMPLE_RATE_HZ
  // seconds forward in time,  (PI * 2 * f)/SAMPLE_RATE_HZ describes how much each
  // frequency jumps for each new sample.
  for (int i = 0 ; i < NOTE_COUNT; i++) {
    frequencySteps[i] = (PI * 2 * noteFrequencies[i])/SAMPLE_RATE_HZ;
  }

  // Start streaming sample to the DAC on a separate thread.
  xTaskCreatePinnedToCore(
                    soundLoop,   /* Function to run on this thread */
                    "unused", /* Name of the thread */
                    10000,      /* Thread stack size */
                    NULL,
                    0,          /* Priority of the task */
                    NULL,
                    0);  /* Which core the thread should run on */
}

void loop() {
  // Read each potentiometer and store it.  These values are in the range 0...1023
  for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
    pots[i] = analogRead(potentiometerPins[i]);
  }
  // Map the volume potentiometer to the range 0...1
  volume = map(pots[POTENTIOMETER_VOLUME], 0, 1023, 0, 100)/100.0;

  // Get the tempo, which ranges from [60, 800] ms per beat.
  tempoDurationMs = map(pots[POTENTIOMETER_TEMPO], 0, 1023, 800, 60);

  squareWaveInfluence = map(pots[2], 0, 1023, 0, 100)/100.0;

  // This value shifts the pitch from 1x...2x the "normal" pitch
  pitchShift = map(pots[1], 0, 1023, 100, 200)/100.0;

  // Once we've reached the next beat, advance the sequencer.
  unsigned long time = millis();
  if (time >= lastAdvance + tempoDurationMs || time < lastAdvance) {
    lastAdvance = time;
    advanceSequencerColumn();
  }
}

/**
 * Gets called every time the sequencer advances.
 */
void advanceSequencerColumn() {
  // Increment and wrap the current column.
  currentColumn = (++currentColumn)%(SEQUENCER_COLUMN_COUNT);
  
  // Reset each button's state and trigger the interrupt to read them again.
  for (int note = 0 ; note < NOTE_COUNT; note++) {
    if (buttonState[note]) {
      gridState[currentColumn][note] ^= true;
      buttonState[note] = digitalRead(buttonPins[note]);
    }
  }

  // Redraw the pixels of the LED matrix.
  ledMatrix.clear();
  for (int column = 0; column < SEQUENCER_COLUMN_COUNT; column++) {
    for (int note = 0; note < NOTE_COUNT; note++) {
      if (column == currentColumn || gridState[column][note]) {
        ledMatrix.setPixel(column, note);
      }
    }
  }
  ledMatrix.commit();
}

/**
 * Scrolls the word "Jambox" across the screen.
 */
void scrollJamboxText() {
  ledMatrix.setText("Jambox");
  for (int i=0; i<74; i++) {
    ledMatrix.clear();
    ledMatrix.scrollTextLeft();
    ledMatrix.drawText();
    ledMatrix.commit();
    delay(10);
  }
}

/**
 * Executes as a thread a pushes I2S sampels as fast as the 
 * bus will take them.
 */
void soundLoop( void * pvParameters ){
  while(true){
    writeSamples();
  }
}

/**
 * Writes a buffer of size BUFFER_SIZE to the I2S bus.
 */
static void writeSamples() {
    short samples[BUFFER_LENGTH * 2] = {0};
    for(int i = 0; i < BUFFER_LENGTH * 2; i += 2) {
        double sample = 0;
        int number = 0;
        for (int i = 0; i < NOTE_COUNT; i++) {
          if (gridState[currentColumn][i] > 0) {
            frequencyCurrent[i] += frequencySteps[i]*pitchShift;

            // If frequencyCurrent starts getting big, then the 
            // accuracy of small increments starts to suffer.  Trim it 
            // down since sin(a + n*2*PI) == sin(a)
            if (frequencyCurrent[i] > PI2) {
              frequencyCurrent[i] -= PI2;
            }

            // Compute the sin wave.
            double contribution = sin(frequencyCurrent[i]);
            
            // If the square wave should contribute, add it here.
            if (squareWaveInfluence > 0) {
              if (contribution > 0) {
                contribution = squareWaveInfluence + contribution * (1-squareWaveInfluence);
              } else {
                contribution = -1*squareWaveInfluence + contribution * (1-squareWaveInfluence);
              }
            }
            sample += contribution * SAMPLE_SCALE * volume;
            number++;
          }
        }

        // This kind of channel mixing would normally distort the audio, but we can 
        // get away with it because our signals are very simple.
        if (number > 1) {
          sample /= number;
        }

        // I2S format is to send two values for each sample, the left and right channels.
        samples[i] = (short)sample;
        samples[i+1] = (short)sample;
    }
    // Write out the samples.
    size_t bytesWritten = 0;
    i2s_write(I2S_NUM, samples, BUFFER_LENGTH * 4, &bytesWritten, portMAX_DELAY);
}

/**
 * Gets triggered any time a button is pressed and stores that the button was pressed.
 */
void buttonInterrupt() {
  for (int note = 0; note < NOTE_COUNT; note++){
    if (digitalRead(buttonPins[note]) == HIGH) {
      buttonState[note] = true;
    }
  }
}

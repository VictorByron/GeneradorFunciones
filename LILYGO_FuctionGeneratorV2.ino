#include <FS.h>
#include <TFT_eSPI.h>
#include <MD_AD9833.h>
#include <AceButton.h>
#include <RotaryEncoder.h>

#include "interface_config.h"

using namespace ace_button;

// Fuente en SPIFFS
#define FONT "TavirajLight18"

// Config. encoder LILYGO S3 DISPLAY:
/*const uint8_t ENCODER_DT = 44;
const uint8_t ENCODER_CK = 18;
const uint8_t ENCODER_SW = 43;*/

// Config. encoder Módulo genérico ESP32 S3:
const uint8_t ENCODER_CK = 18;
const uint8_t ENCODER_DT = 17;
const uint8_t ENCODER_SW = 16;

// Config. módulo AD9833
const uint8_t DAT = 11;   // SPI_D en la placa LILYGO-T Display S3
const uint8_t CLK = 12;   // SPI_CLK en la placa LILYGO-T Display S3
const uint8_t FNC = 10;

// Config. display
TFT_eSPI    tft = TFT_eSPI();
TFT_eSprite spr_banner = TFT_eSprite(&tft);
TFT_eSprite spr_button = TFT_eSprite(&tft);
TFT_eSprite spr_screen = TFT_eSprite(&tft);

// Control del módulo generador de señales
MD_AD9833 fuction_generator(FNC);

// Control del encoder
RotaryEncoder *encoder = NULL;

// Control del botón del encoder
AceButton option_button(ENCODER_SW);

// Dimensiones y espaciados
int x_margin, y_margin;
int label_height, label_width;
int xpos_buttons;
int border_radius = 5;

// variables globales:
int old_encoder_pos;
uint8_t selector = 0;

bool mode = true; // mode = 1 -> selección, modo = 0 -> configuración
bool update_screen = false;
bool update_buttons = false;
bool update_banner = false;

uint8_t waveform = 0;
float frequency = 100;
uint8_t multiplier = 0;
bool output = false;

float temp_freq;

const String labels_multiplier[] = {"10^0", "10^2", "10^3", "10^4", "10^5", "10^6"};
const String labels_waveforms[] = {"Sine", "Suare", "Triangular"};
String labels_buttons[] = {"Wave", "Inc/Dec", "", "output"};
float adjust_freq[] = {1E0, 1E2, 1E3, 1E4, 1E5, 1E6};

String label_wave = labels_waveforms[waveform];
String label_freq = String(frequency,0) + " Hz";

static MD_AD9833::mode_t generator_modes[] =
  {
    MD_AD9833::MODE_SINE,
    MD_AD9833::MODE_SQUARE1,
    MD_AD9833::MODE_TRIANGLE
  };

// Interrupciones del encoder
void IRAM_ATTR checkPosition(){
  encoder->tick();
}

// Control de eventos del botón
void handleEventButton(AceButton* b, uint8_t e, uint8_t s){
  if(e == AceButton::kEventClicked){ // Inverte el modo de control del encoder
    if(mode && selector == SIGNAL_OUTPUT) // Modo selección y selector sobre el botón "Output"
      output = true;
    else
      mode = !mode;
  }
}

// Dibuja un banner animado
void updateBanner(uint16_t x, uint16_t y){
  spr_banner.fillSprite(BACKGROUND_COLOR);
  spr_banner.fillSmoothRoundRect(0, 0, spr_banner.width(), spr_banner.height(), border_radius, BANNER_COLOR, BACKGROUND_COLOR);
  spr_banner.setCursor(5, (spr_banner.height() - spr_banner.fontHeight())/2);
  spr_banner.print("CH0 - Freq: " + label_freq);
  spr_banner.pushSprite(x, y);
  update_banner = false;
}

// Dibuja un botón
void drawBtn(uint16_t x, uint16_t y, String label, bool selected){

  uint16_t color, text_color;

  if(selected){
    color = LABEL_SELECTED_COLOR;
    text_color = TEXT_SELECTED_COLOR;
  }
  else{
    color = LABEL_COLOR;
    text_color = TEXT_COLOR;
  }
  
  spr_button.createSprite(label_width - 2*x_margin, label_height - 2*y_margin);
  spr_button.fillSprite(BACKGROUND_COLOR);
  spr_button.fillSmoothRoundRect(0, 0, spr_button.width(), spr_button.height(), border_radius, color, BACKGROUND_COLOR);
  spr_button.setTextColor(text_color, color);
  spr_button.setCursor((spr_button.width() - spr_button.textWidth(label))/2, (spr_button.height() - spr_button.fontHeight())/2);
  spr_button.print(label);
  spr_button.pushSprite(x, y);
  spr_button.deleteSprite();
}

void updateButtons(uint8_t sel){
  for(int y_pos = 0; y_pos < 4; y_pos++)
    drawBtn(xpos_buttons, y_margin + y_pos*label_height, labels_buttons[y_pos], y_pos == sel);

  update_buttons = false;
}

void updateScreen(int x, int y){
  spr_screen.fillSprite(BACKGROUND_COLOR);
  
  int w_draw_area = spr_screen.width();
  int h_draw_area = spr_screen.height() - (2*spr_screen.fontHeight()+8);
  
  // Dibuja la cuadricula de fondo
  spr_screen.drawRect(0, 0, w_draw_area, h_draw_area, GRID_COLOR);
  // Lineas punteadas verticales
  int x_step = w_draw_area/8;
  for(int line = x_step; line < w_draw_area; line += x_step)
    for(int h_pos = 1; h_pos <= h_draw_area - 4; h_pos += 6)
      spr_screen.drawFastVLine(line, h_pos, 2, GRID_COLOR);
  // Lineas punteadas horizontales
  int y_step = h_draw_area/4;
  for(int line = y_step; line < h_draw_area; line += y_step)
    for(int w_pos = 1; w_pos <= w_draw_area - 2; w_pos += 6)
      spr_screen.drawFastHLine(w_pos, line, 2, GRID_COLOR);
  // Dibuja la forma de onda
  drawWaveform(waveform, w_draw_area, h_draw_area);
  
  spr_screen.setCursor(5, h_draw_area+5);
  spr_screen.print("Signal: " + label_wave);
  spr_screen.setCursor(5, h_draw_area + spr_screen.fontHeight()+5);
  spr_screen.print("Amplitude: 650 mVpp");
  spr_screen.pushSprite(x, y);
  update_screen= false;
}

void drawWaveform(uint8_t waveform, int w, int h){
  int x1 = 1;
  switch(waveform){
    case SINE:
      for(int x = 0; x <= 720; x++)
        spr_screen.drawPixel(map(x, 0, 720, 0, w), (h/2)-sin(x*PI/180)*(h/2), SINE_WAVE_COLOR);
    break;

    case SQUARE:
      for(int x = 0; x < 4; x++){
        int y = x%2 ? 1 : h - 2;
        spr_screen.drawFastVLine(1 + x*(w/4), 1, h-2, SQRT_WAVE_COLOR);
        spr_screen.drawFastHLine(1 + x*(w/4), y, w/4, SQRT_WAVE_COLOR);
      }
    break;

    case TRIANGULAR:
      for(int x = 0; x < 4; x++){
        spr_screen.drawLine(1 + x*(w/4), h-1, 1 + x1*w/8, 1, TRGL_WAVE_COLOR);
        spr_screen.drawLine(1 + x1*w/8, 1, 1 + (x+1)*(w/4), h-1, TRGL_WAVE_COLOR);
        x1 += 2;
      }
    break;

    default:
    break;
  }
}

void setup() {
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  if (!SPIFFS.begin()) {
    tft.print("SPIFFS initialisation failed!");
    while (1) yield();
  }
  tft.println("\r\nSPIFFS available!");

  bool font_missing = false;
  if (SPIFFS.exists("/TavirajLight18.vlw")    == false) font_missing = true;

  if (font_missing){
    tft.println("\r\nFont missing in SPIFFS, did you upload it?");
    while(1) yield();
  }
  else {tft.println("\r\nNew font added.");}
  delay(800);

  tft.fillScreen(TFT_BLACK);

  x_margin = tft.width() * 0.01;
  y_margin = tft.height() * 0.02;

  label_width = tft.width()/3;
  label_height = tft.height()/4;

  xpos_buttons = 2*(label_width + x_margin);

  tft.setCursor(0,0);
  tft.println("x_margin: " + String(x_margin));
  tft.println("y_margin: " + String(y_margin));
  tft.println("label_width: " + String(label_width));
  tft.println("label_height: " + String(label_height));

  delay(800);

  tft.fillScreen(TFT_BLACK);
  // Crea y configura el sprite del banner
  // Ancho banner = 2*label_width - (borde en x)
  spr_banner.createSprite(2*(label_width - x_margin), label_height - 2*y_margin);
  spr_banner.setColorDepth(16);
  spr_banner.loadFont(FONT);
  spr_banner.setTextWrap(false);
  spr_banner.setTextColor(TEXT_COLOR, BANNER_COLOR);

  // Configuración del sprite de los botones
  spr_button.setColorDepth(16);
  spr_button.loadFont(FONT);

  // Crea y configura el sprite de la pantalla
  spr_screen.createSprite(2*(label_width - x_margin), 3*label_height - 2*y_margin);
  spr_screen.setColorDepth(16);
  spr_screen.loadFont(FONT);
  spr_screen.setTextColor(TEXT_COLOR, BACKGROUND_COLOR);

  // Dibuja estado inicial de la etiqueta principal
  updateBanner(x_margin, y_margin);

  // Dibuja estado inicial de la pantalla
  updateScreen(x_margin, 2*y_margin + label_height);

  // Dibuja estado inicial de los botones
  labels_buttons[MULTIPLIER] = labels_multiplier[multiplier];
  updateButtons(selector);

  // Configuración del encoder
  encoder = new RotaryEncoder(ENCODER_DT, ENCODER_CK, RotaryEncoder::LatchMode::TWO03);
  attachInterrupt(digitalPinToInterrupt(ENCODER_DT), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CK), checkPosition, CHANGE);

  // Configuración del botón del encoder
  pinMode(ENCODER_SW, INPUT_PULLUP);
  ButtonConfig* buttonConfig = option_button.getButtonConfig();
  buttonConfig->setEventHandler(handleEventButton);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);

  // Inicia el funcionamiento del generador de señales
  fuction_generator.begin();
  fuction_generator.setMode(MD_AD9833::MODE_OFF);

  if(option_button.isPressedRaw())  delay(1000);
}

void loop() {
  option_button.check();

  int new_encoder_pos = encoder->getPosition();

  // Cambio en el encoder
  if(old_encoder_pos != new_encoder_pos){
    // calcula al dirección de giro
    int dir = (int)encoder->getDirection();
    // Modo selección:
    if(mode){
      old_encoder_pos = new_encoder_pos;
      
      if(dir > 0 && selector < 3) selector++;
      else if(dir < 0 && selector > 0) selector--;
      
      update_buttons = true;
    }
    // Modo configuración:
    else{
      // Se "guarda" la posición del selector estableciendo la posición del
      // encoder en la última posición antes de entrar al bloque else,
      // ya que al utilizar el mismo encoder para cambiar tanto las opciones
      // como los modos de funcionamiento, el programa "pensaría" que hubo un
      // cambio en la selección de la opción en lugar de la configuración
      encoder->setPosition(old_encoder_pos);

      switch(selector){
        case WAVEFORM:
          if(dir > 0 && waveform < 2) waveform++;
          else if(dir < 0 && waveform > 0) waveform--;
          label_wave = labels_waveforms[waveform];
          update_screen = true; 
        break;
    
        case FREQUENCY:
          temp_freq = frequency;
          if(dir > 0 && frequency <= 12.5E6){
            frequency += adjust_freq[multiplier];
            if(frequency > 12.5E6)  frequency = temp_freq;
          }
          else if(dir < 0 && frequency > 0){
            frequency -= adjust_freq[multiplier];
            if(frequency < 0) frequency = temp_freq;
          }

          char buf[10];
          label_freq = "";
          
          if(frequency < 1E3)
            label_freq = String((int)frequency) + " Hz";
            
          else{
            if(frequency >= 1E3 && frequency < 10E3)
              sprintf(buf, "%2.3f kHz", frequency/1E3);
            else if(frequency >= 10E3 && frequency < 10E5)
              sprintf(buf, "%3.2f kHz", frequency/1E3);
            else
              sprintf(buf, "%2.3f MHz", frequency/1E6);
              
            label_freq.concat(buf);
          }
          update_banner = true;
          
        break;
    
        case MULTIPLIER:
          if(dir > 0 && multiplier < 5) multiplier++;
          else if(dir < 0 && multiplier > 0) multiplier--;
          labels_buttons[MULTIPLIER] = labels_multiplier[multiplier];
          update_buttons = true;
        break;

        default:
        break;
      }
    }
  }

  if(update_buttons)  updateButtons(selector);
  if(update_screen)   updateScreen(x_margin, 2*y_margin + label_height);
  if(update_banner)   updateBanner(x_margin, y_margin);

  if(output){
    fuction_generator.setMode(generator_modes[waveform]);
    fuction_generator.setFrequency(MD_AD9833::CHAN_0, frequency);
    output = false;
  }

}

#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "FS.h" // SD Card ESP32
#include "SD_MMC.h" // SD Card ESP32
#include "soc/soc.h" // Disable brownout problems
#include "soc/rtc_cntl_reg.h" // Disable brownout problems
#include "driver/rtc_io.h"
#include <EEPROM.h> // Read and write from flash memory
#include <mbedtls/base64.h> // Base64 encoding using mbedtls

#define EEPROM_SIZE 1

// Pin definition for CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define LED_PIN 4 // Pin for the LED (GPIO 4)

int pictureNumber = 0;
const char* ssid = "Khu S";
const char* password = "khu@s2022";

WebServer server(80);

// Define the cropping region (adjust these values based on your requirements)
int cropX = 100; // X coordinate of the top-left corner of the crop region
int cropY = 50; // Y coordinate of the top-left corner of the crop region
int cropWidth = 20; // Width of the crop region
int cropHeight = 20; // Height of the crop region

// Function declarations
void handleRoot();
void handleCapture();
String base64Encode(uint8_t* data, size_t len);
bool saveImageToSD(const char* filename, uint8_t* data, size_t length);
void cropImage(camera_fb_t* fb, int x, int y, int w, int h, String &croppedImageBase64);

// ------------------------------------------------------------
// Function: base64Encode
// Description: Encodes binary data to a Base64 string
// Parameters:
// - data: pointer to the binary data
// - len: length of the binary data
// Returns: Base64 encoded string
// ------------------------------------------------------------
String base64Encode(uint8_t* data, size_t len) {
size_t encodedLen;
mbedtls_base64_encode(NULL, 0, &encodedLen, data, len);

char* encodedData = (char*)malloc(encodedLen);
if (encodedData == NULL) {
return String("");
}

mbedtls_base64_encode((unsigned char*)encodedData, encodedLen, &encodedLen, data, len);
String encodedString = String(encodedData);
free(encodedData);
return encodedString;
}

// ------------------------------------------------------------
// Function: saveImageToSD
// Description: Saves image data to a file on the SD card
// Parameters:
// - filename: name of the file to save
// - data: pointer to the image data
// - length: length of the image data
// Returns: true if the image was saved successfully, false otherwise
// ------------------------------------------------------------
bool saveImageToSD(const char* filename, uint8_t* data, size_t length) {
File file = SD_MMC.open(filename, FILE_WRITE);
if (!file) {
Serial.println("Failed to open file for writing");
return false;
}

file.write(data, length);
file.close();
return true;
}

// ------------------------------------------------------------
// Function: cropImage
// Description: Crops an image to the specified region and encodes it to Base64
// Parameters:
// - fb: pointer to the camera frame buffer
// - x, y: top-left corner coordinates of the cropping region
// - w, h: width and height of the cropping region
// - croppedImageBase64: reference to the string to store the Base64 encoded cropped image
// ------------------------------------------------------------
void cropImage(camera_fb_t* fb, int x, int y, int w, int h, String &croppedImageBase64) {
uint8_t* cropped_buf = (uint8_t*)malloc(w * h * 3); // Allocate buffer for cropped image
int index = 0;

for (int i = y; i < y + h; i++) {
for (int j = x; j < x + w; j++) {
int pixel_index = (i * fb->width + j) * 3;
cropped_buf[index++] = fb->buf[pixel_index]; // Copy Red value
cropped_buf[index++] = fb->buf[pixel_index + 1]; // Copy Green value
cropped_buf[index++] = fb->buf[pixel_index + 2]; // Copy Blue value
}
}

// Convert cropped image to JPEG
size_t jpeg_buf_len = 0;
uint8_t *jpeg_buf = NULL;
if (!fmt2jpg(cropped_buf, w * h * 3, w, h, PIXFORMAT_RGB888, 90, &jpeg_buf, &jpeg_buf_len)) {
Serial.println("JPEG compression failed");
free(cropped_buf);
return;
}

// Save the cropped image to SD
String croppedFilename = "/cropped_" + String(pictureNumber++) + ".jpg";
saveImageToSD(croppedFilename.c_str(), jpeg_buf, jpeg_buf_len);

// Convert JPEG buffer to Base64
croppedImageBase64 = base64Encode(jpeg_buf, jpeg_buf_len);

free(cropped_buf); // Free the allocated buffer
free(jpeg_buf); // Free the JPEG buffer
}

// ------------------------------------------------------------
// Function: handleRoot
// Description: Handles the root web page
// ------------------------------------------------------------
void handleRoot() {
server.send(200, "text/html", "<form action=\"/capture\" method=\"POST\"><button type=\"submit\">Capture Image</button></form>");
}

// ------------------------------------------------------------
// Function: handleCapture
// Description: Captures an image from the camera, crops it, and sends it back as Base64 encoded strings
// ------------------------------------------------------------
void handleCapture() {
digitalWrite(LED_PIN, HIGH); // Turn on LED
delay(200); // Wait 200 ms to allow the LED to turn on

camera_fb_t * fb = esp_camera_fb_get();
if (!fb) {
server.send(500, "text/plain", "Camera capture failed");
digitalWrite(LED_PIN, LOW); // Turn off LED
return;
}

// Save the original image to SD
String originalFilename = "/original_" + String(pictureNumber++) + ".jpg";
saveImageToSD(originalFilename.c_str(), fb->buf, fb->len);

// Convert the original image to Base64
String originalImageBase64 = base64Encode(fb->buf, fb->len);

// Process and get the cropped image as Base64
String croppedImageBase64;
cropImage(fb, cropX, cropY, cropWidth, cropHeight, croppedImageBase64);

esp_camera_fb_return(fb);
digitalWrite(LED_PIN, LOW); // Turn off LED

// Create HTML content with the images
String htmlResponse = "<h1>Image Capture</h1>";
htmlResponse += "<div>Original Image:<br><img src='data:image/jpeg;base64," + originalImageBase64 + "'/></div>";
htmlResponse += "<div>Cropped Image:<br><img src='data:image/jpeg;base64," + croppedImageBase64 + "'/></div>";

server.send(200, "text/html", htmlResponse);
}

// ------------------------------------------------------------
// Function: setup
// Description: Initializes the ESP32, camera, and web server
// ------------------------------------------------------------
void setup() {
WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

Serial.begin(115200);

// Connect to WiFi
WiFi.begin(ssid, password);
while (WiFi.status() != WL_CONNECTED) {
delay(500);
Serial.print(".");
}
Serial.println("Connected to WiFi");

Serial.print("Web server started, visit: http://");
Serial.println(WiFi.localIP());

// Camera configuration
camera_config_t config;
config.ledc_channel = LEDC_CHANNEL_0;
config.ledc_timer = LEDC_TIMER_0;
config.pin_d0 = Y2_GPIO_NUM;
config.pin_d1 = Y3_GPIO_NUM;
config.pin_d2 = Y4_GPIO_NUM;
config.pin_d3 = Y5_GPIO_NUM;
config.pin_d4 = Y6_GPIO_NUM;
config.pin_d5 = Y7_GPIO_NUM;
config.pin_d6 = Y8_GPIO_NUM;
config.pin_d7 = Y9_GPIO_NUM;
config.pin_xclk = XCLK_GPIO_NUM;
config.pin_pclk = PCLK_GPIO_NUM;
config.pin_vsync = VSYNC_GPIO_NUM;
config.pin_href = HREF_GPIO_NUM;
config.pin_sscb_sda = SIOD_GPIO_NUM;
config.pin_sscb_scl = SIOC_GPIO_NUM;
config.pin_pwdn = PWDN_GPIO_NUM;
config.pin_reset = RESET_GPIO_NUM;
config.xclk_freq_hz = 20000000;
config.pixel_format = PIXFORMAT_JPEG;

if (psramFound()) {
config.frame_size = FRAMESIZE_QVGA; // Use a lower resolution
config.jpeg_quality = 12; // Adjust JPEG quality to balance between size and clarity
config.fb_count = 1;
} else {
config.frame_size = FRAMESIZE_SVGA; // Use a lower resolution
config.jpeg_quality = 20; // Adjust JPEG quality to balance between size and clarity
config.fb_count = 1;
}

esp_err_t err = esp_camera_init(&config);
if (err != ESP_OK) {
Serial.printf("Camera init failed with error 0x%x", err);
return;
}

pinMode(LED_PIN, OUTPUT);
digitalWrite(LED_PIN, LOW);

if (!SD_MMC.begin("/sdcard", true)) {
Serial.println("SD Card Mount Failed");
return;
}

uint8_t cardType = SD_MMC.cardType();
if (cardType == CARD_NONE) {
Serial.println("No SD Card attached");
return;
}

// Define web server routes
server.on("/", HTTP_GET, handleRoot);
server.on("/capture", HTTP_POST, handleCapture);

server.begin();
}

// ------------------------------------------------------------
// Function: loop
// Description: Handles incoming client requests
// ------------------------------------------------------------
void loop() {
server.handleClient();
}
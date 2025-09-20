#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <time.h>

// --- Timezone for Bern / Switzerland (Europe/Zurich) ---
#ifndef TZ_STRING
#define TZ_STRING "CET-1CEST,M3.5.0/2,M10.5.0/3"
#endif

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

//============ SIZE / POSITIONS ========================
#define SCALE      0.85f        // tach & speed size
#define AUX_SCALE  0.70f        // mini-gauge size

// Tachometer (RPM)
#define TACH_X    165
#define TACH_Y     95
// Speedometer (km/h)
#define SPEEDO_X  290
#define SPEEDO_Y  110
// RIGHT-ARC mini gauge (placed left of tach)
#define AUX_X      50
#define AUX_Y     115
//======================================================

//............INPUT PINS............switches and buttons
#define THROTTLE   14
#define BRAKE      44
#define LEFT       17
#define RIGHT      18
#define SHORT      16
#define LONG       21
#define GEARUP     14
#define GEARDOWN   13
#define HORN       10
#define BRIGHTNESS 16

//..........OUTPUT PINS............lights, pointers, horn
#define left_pointer  1
#define right_pointer 11
#define head_lights   2
#define buzzer        3

//==================== Palette (RGB565)
#define BACK   0x0000
#define WHITE  0xFFFF
#define GREY1  0x6B6D
#define GREY2  0x4208
#define ORANGE 0xFD20
#define RED    0xF800
#define GREEN  0x07E0
#define GOLD   0xFFD700  // warm gold
//======================================================

#define backColor   BACK
#define needleColor ORANGE

//================ Font sizes ==========================
const int RING_NUM_FONT    = 1;
const int LABEL_FONT       = 2;
const int TACH_CENTER_FONT = 4;
// Banner fonts
const int CAR_FONT   = 4;  // unchanged (car name)
const int DATE_FONT  = 4;  // bigger date
const int TIME_FONT  = 6;  // bigger time
//======================================================

// Base dimensions (unscaled originals)
const int BASE_R  = 72;
const int BASE_IR = 70;
const int OFF_O1 = 10;
const int OFF_O2 = 14;
const int OFF_O3 = 26;
const int OFF_O4 = 38;
const int RIM_PLUS  = 2;
const int RIM_IN1   = 5;
const int RIM_IN2   = 6;

// Scaled dimensions (tach/speedo)
int r  = (int)round(BASE_R  * SCALE);
int ir = (int)round(BASE_IR * SCALE);
int o1 = (int)round(OFF_O1  * SCALE);
int o2 = (int)round(OFF_O2  * SCALE);
int o3 = (int)round(OFF_O3  * SCALE);
int o4 = (int)round(OFF_O4  * SCALE);
int rimPlus = (int)round(RIM_PLUS * SCALE);
int rimIn1  = (int)round(RIM_IN1  * SCALE);
int rimIn2  = (int)round(RIM_IN2  * SCALE);

// Aux (mini) radii
int aux_r  = (int)round(BASE_R  * AUX_SCALE);
int aux_ir = (int)round(BASE_IR * AUX_SCALE);

// Needle/tick thickness
int needleThick = (int)round(3 * SCALE);
int needleTail  = (int)round(3 * SCALE);
int tickThickMajor = (int)round(2 * SCALE);
int tickThickMinor = (int)round(1 * SCALE);
#define CLAMP_MIN(v,minv) do{ if((v)<(minv)) (v)=(minv); }while(0)

// ===== Parameter: adjust needle length (pixels) =====
// Positive value shortens the needle by this many pixels.
// Negative value lengthens it by that many pixels.
const int NEEDLE_TIP_ADJUST = -15;

// Geometry arrays
float x[360],y[360],px[360],py[360],lx[360],ly[360],nxp[360],nyp[360];
float x2[360],y2[360],px2[360],py2[360],lx2[360],ly2[360],nx2p[360],ny2p[360];

double rad=0.01745;
float sA;   // speedo angle
float rA;   // tach angle

int blinkPeriod=500;
unsigned long currentTimeL=0, currentTimeR=0;
int brightnesses[5]={40,80,120,150,240};
int selectedBrightness=3;
int deb1=0,deb2=0,debB=0;

int gearMaxSpeed[8]={12,0,60,90,120,150,190,236};
String gears[8]={"R","N","1","2","3","4","5","6"};
int selectedGear=1;

// state
bool leftPointer=0, rightPointer=0, braking=false;
int  lights=0;
float speedAngle=0;
float rpmAngle=5;

// ====== Animated banner (top) ======
const char* BANNER_TEXT = "PORSCHE 964 Carrera 2";
int bannerX = 320;                 // start off-screen right
const int bannerPaddingTop = 2;    // pixels from top

// ---- Time / NTP ----
const char* NTP_SERVER = "pool.ntp.org";
char dateStr[24] = "";             // e.g. "Sat 20 Sep 2025"
char timeStr[8]  = "";             // e.g. "14:32"
unsigned long lastTimeTick = 0;    // 1 Hz update

// widths for smooth scrolling
int wCar = 0, wSp1 = 0, wDate = 0, wSp2 = 0, wTime = 0, bannerFullWidth = 0;

// spacers between segments
const char* BANNER_SPACER1 = "   ";
const char* BANNER_SPACER2 = " ";

// ---- WiFi auto-adjust / portal control ----
WiFiManager wm;                    // global so we can drive non-blocking portal
bool portalRunning = false;
unsigned long lastWifiCheck = 0;
const unsigned long WIFI_CHECK_MS = 5000;      // check every 5s
const unsigned long LONG_PRESS_MS = 2000;      // hold BRIGHTNESS 2s to open portal
unsigned long pressStart = 0;
bool pressActive = false;

// ---------- helpers ----------
inline void drawTick(int cx, int cy, int Router, int Rin, float deg, int thick, uint16_t col){
  float sx = cx + Router * cos(rad*deg);
  float sy = cy + Router * sin(rad*deg);
  float ex = cx + Rin    * cos(rad*deg);
  float ey = cy + Rin    * sin(rad*deg);
  sprite.drawWedgeLine(sx,sy,ex,ey,thick,thick,col);
}

// Draw a needle but adjust tip length by NEEDLE_TIP_ADJUST (pixels).
inline void drawNeedleAdjusted(int cx, int cy, float ex, float ey, uint16_t col){
  float dx = ex - cx;
  float dy = ey - cy;
  float len = sqrtf(dx*dx + dy*dy);
  if (len <= 1) { // fallback
    sprite.drawWedgeLine(cx, cy, ex, ey, needleThick, needleTail, col);
    return;
  }
  float newLen = len - NEEDLE_TIP_ADJUST;   // +shorter, -longer
  if (newLen < 1) newLen = 1;
  float scale = newLen / len;
  float nx_ = cx + dx * scale;
  float ny_ = cy + dy * scale;
  sprite.drawWedgeLine(cx, cy, nx_, ny_, needleThick, needleTail, col);
}

// Try to (re)format local time string once per second.
inline void updateTimeStringOncePerSecond() {
  if (millis() - lastTimeTick < 1000) return;
  lastTimeTick = millis();

  const char* fallbackDate = "— — — — — —";
  const char* fallbackTime = "— —";

  struct tm tmnow;
  if (getLocalTime(&tmnow, 200)) {                 // 200 ms timeout
    strftime(dateStr, sizeof(dateStr), "%a %d %b %Y", &tmnow); // "Sat 20 Sep 2025"
    strftime(timeStr, sizeof(timeStr), "%H:%M", &tmnow);       // "14:32"
  } else {
    strncpy(dateStr, fallbackDate, sizeof(dateStr));
    strncpy(timeStr, fallbackTime, sizeof(timeStr));
    dateStr[sizeof(dateStr)-1] = 0;
    timeStr[sizeof(timeStr)-1] = 0;
  }

  // Measure each segment using the intended font for that segment
  sprite.setTextFont(CAR_FONT);
  wCar  = sprite.textWidth(BANNER_TEXT,    CAR_FONT);

  sprite.setTextFont(CAR_FONT);
  wSp1  = sprite.textWidth(BANNER_SPACER1, CAR_FONT);

  sprite.setTextFont(DATE_FONT);
  wDate = sprite.textWidth(dateStr,        DATE_FONT);

  sprite.setTextFont(CAR_FONT);
  wSp2  = sprite.textWidth(BANNER_SPACER2, CAR_FONT);

  sprite.setTextFont(TIME_FONT);
  wTime = sprite.textWidth(timeStr,        TIME_FONT);

  bannerFullWidth = wCar + wSp1 + wDate + wSp2 + wTime;

  // Restore a sensible default font after measuring (optional)
  sprite.setTextFont(CAR_FONT);
}

// WiFi ensure + auto-reconnect + optional config portal
inline void ensureWifi() {
  // Long-press BRIGHTNESS button to open or close config portal
  if (digitalRead(BRIGHTNESS) == LOW) {
    if (!pressActive) { pressActive = true; pressStart = millis(); }
    else if (!portalRunning && (millis() - pressStart > LONG_PRESS_MS)) {
      // Start non-blocking config portal
      wm.setConfigPortalBlocking(false);
      wm.setConfigPortalTimeout(180); // auto-close after 3 minutes
      portalRunning = wm.startConfigPortal("P964-Setup");
      // Reset press state to avoid re-trigger
      pressActive = false;
    }
  } else {
    if (pressActive && (millis() - pressStart < LONG_PRESS_MS)) {
      // short press cycles brightness (existing behavior below handles it)
    }
    pressActive = false;
  }

  // If portal is running, process it
  if (portalRunning) {
    wm.process(); // keep portal responsive
    if (WiFi.status() == WL_CONNECTED) {
      portalRunning = false; // portal will stop once connected
      wm.stopConfigPortal();
    }
    return; // don't also run reconnect logic this cycle
  }

  // Periodic connectivity watchdog
  if (millis() - lastWifiCheck >= WIFI_CHECK_MS) {
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect(); // quick try with saved creds
    }
  }
}

// ---------- speedo ----------
void drawSpeedoLikePhoto()
{
  sprite.drawSmoothArc(SPEEDO_X,SPEEDO_Y,r,ir,30,330,GREY1,backColor);
  sprite.drawSmoothArc(SPEEDO_X,SPEEDO_Y,r-rimIn1,r-rimIn2,30,330,GREY1,backColor);

  int bandOuter = r - (int)round(0.55f * rimIn1);
  int bandInner = bandOuter - (int)round(2 * SCALE);
  sprite.drawSmoothArc(SPEEDO_X,SPEEDO_Y,bandOuter,bandInner,30,50,GREY1,backColor);

  for(int v=0; v<=300; v+=10){
    int idx=v; bool major20=(v%20==0);
    int thickA=major20?tickThickMajor:tickThickMinor;
    drawTick(SPEEDO_X,SPEEDO_Y,r-o1,r-o2,idx,thickA,WHITE);
    if(major20 && v!=0){
      sprite.setTextColor(WHITE,backColor);
      sprite.drawString(String(v),lx2[idx],ly2[idx],RING_NUM_FONT);
    }
  }

  sprite.setTextColor(WHITE,backColor);
  sprite.drawString("km", SPEEDO_X, SPEEDO_Y-28, LABEL_FONT);
  sprite.fillRoundRect(SPEEDO_X-28,SPEEDO_Y-16,56,18,3,BACK);
  sprite.drawRoundRect(SPEEDO_X-28,SPEEDO_Y-16,56,18,3,GREY1);
  sprite.setTextColor(WHITE,BACK);
  sprite.drawString("117961", SPEEDO_X, SPEEDO_Y-7, LABEL_FONT);
}

// ---------- MINI GAUGE ----------
void drawRightArcMiniGauge(int cx, int cy)
{
  auto normDeg=[](int d){d%=360;return d<0?d+360:d;};
  int start=normDeg(20-90); int end=normDeg(140-90);

  auto drawArcSeg=[&](int a0,int a1,int rOut,int rIn,uint16_t col){
    if(a0<a1){
      sprite.drawSmoothArc(cx,cy,rOut,rIn,a0,a1,col,backColor);
    }else{
      sprite.drawSmoothArc(cx,cy,rOut,rIn,a0,360,col,backColor);
      sprite.drawSmoothArc(cx,cy,rOut,rIn,0,a1,col,backColor);
    }
  };

  drawArcSeg(start,end,aux_r,aux_ir,GREY1);
  drawArcSeg(normDeg(start+180),normDeg(end+180),aux_r,aux_ir,GREY1);

  const float baseDegs[6]={140,122,104,86,68,50};
  float degs[6]; for(int i=0;i<6;i++) degs[i]=normDeg((int)baseDegs[i]-90);
  const int lens[6]={7,8,9,10,11,12};

  for(int i=0;i<6;i++){
    int Rout=aux_r-6; int Rin=Rout-lens[i];
    drawTick(cx,cy,Rout,Rin,degs[i],2,WHITE);
    drawTick(cx,cy,Rout,Rin,normDeg(degs[i]+180),2,WHITE);
  }

  sprite.setTextColor(WHITE,backColor);
  const float baseNdeg[6]={46,64,82,100,118,136};
  for(int n=0;n<=5;n++){
    float ndeg=normDeg((int)baseNdeg[n]-90);
    float nr=aux_r-26;
    int nx1=cx+(int)(nr*cos(rad*ndeg));
    int ny1=cy+(int)(nr*sin(rad*ndeg));
    sprite.drawString(String(n),nx1,ny1,RING_NUM_FONT);

    float ndeg2=normDeg((int)ndeg+180);
    int nx2=cx+(int)(nr*cos(rad*ndeg2));
    int ny2=cy+(int)(nr*sin(rad*ndeg2));
    sprite.drawString(String(n),nx2,ny2,RING_NUM_FONT);
  }

  // pivot + two needles
  int pivotX=cx+20; int pivotY=cy+6;
  sprite.fillSmoothCircle(pivotX,pivotY,6,GREY2);

  float angle=55-90; if(angle<0) angle+=360;
  int ex=pivotX+(int)((aux_r-30)*cos(rad*angle));
  int ey=pivotY+(int)((aux_r-30)*sin(rad*angle)); // keep original style
  sprite.drawWedgeLine(pivotX,pivotY,ex,ey,3,3,ORANGE);

  int shiftX=-60;
  int pivotX2=pivotX+shiftX; int pivotY2=pivotY;
  int ex2=pivotX2+(int)((aux_r-30)*cos(rad*angle));
  int ey2=pivotY2+=(int)((aux_r-40)*sin(rad*angle));
  sprite.drawWedgeLine(pivotX2,pivotY2,ex2,ey2,3,3,ORANGE);
  sprite.fillSmoothCircle(pivotX2,pivotY2,3,GREY2);
}

// ---------- DRAW EVERYTHING ----------
void draw()
{
  sprite.fillSprite(backColor);

  // ===== Tach =====
  sprite.drawSmoothArc(TACH_X,TACH_Y,r,ir,30,330,GREY1,backColor);
  sprite.drawSmoothArc(TACH_X,TACH_Y,r-rimIn1,r-rimIn2,30,330,GREY2,backColor);

  for(int i=0;i<15;i++){
    float k=i*0.5f; bool major=(i%2==0); bool redline=(k>=6.5f);
    uint16_t tickColor=redline?RED:WHITE; int idx=i*16;
    sprite.drawWedgeLine(x[idx],y[idx],px[idx],py[idx],
                         major?tickThickMajor:tickThickMinor,
                         tickThickMinor,tickColor);
    if(major){
      sprite.setTextColor(WHITE, backColor);
      sprite.drawString(String((int)k),lx[idx],ly[idx],RING_NUM_FONT);
    }
  }

  // Tach center text
  sprite.setTextColor(WHITE, backColor);
  sprite.setTextDatum(4);
  sprite.drawString("1/min", TACH_X, TACH_Y - 10, LABEL_FONT);
  sprite.drawString("x1000", TACH_X, TACH_Y + 8,  LABEL_FONT);

  // ===== Speedo =====
  drawSpeedoLikePhoto();

  // ===== Mini gauge =====
  drawRightArcMiniGauge(AUX_X,AUX_Y);

  // ===== Needles (apply adjustable tip length) =====
  rA = 2 * rpmAngle * 1.6f;
  drawNeedleAdjusted(TACH_X,   TACH_Y,   nxp[(int)rA],  nyp[(int)rA],  ORANGE);

  float kmh = speedAngle; if(kmh<0) kmh=0; if(kmh>300) kmh=300;
  sA = kmh * 1.0f;
  drawNeedleAdjusted(SPEEDO_X, SPEEDO_Y, nx2p[(int)sA], ny2p[(int)sA], ORANGE);

  // pivots
  sprite.fillSmoothCircle(TACH_X,   TACH_Y,   3, GREY2);
  sprite.fillSmoothCircle(SPEEDO_X, SPEEDO_Y, 3, GREY2);

  // --- Animated banner + date/time (multi-color, scrolls as one line) ---
  sprite.setTextDatum(TL_DATUM);
  int bannerY = bannerPaddingTop;
  int xcursor = bannerX;

  // Car (font 4)
  sprite.setTextFont(CAR_FONT);
  sprite.setTextColor(GOLD, backColor);
  sprite.drawString(BANNER_TEXT, xcursor, bannerY, CAR_FONT);
  xcursor += wCar;

  // spacer 1 (same font as car)
  sprite.setTextFont(CAR_FONT);
  sprite.setTextColor(GOLD, backColor);
  sprite.drawString(BANNER_SPACER1, xcursor, bannerY, CAR_FONT);
  xcursor += wSp1;

  // Date (bigger font 6)
  sprite.setTextFont(DATE_FONT);
  sprite.setTextColor(WHITE, backColor);
  sprite.drawString(dateStr, xcursor, bannerY, DATE_FONT);
  xcursor += wDate;

  // spacer 2 (car font)
  sprite.setTextFont(CAR_FONT);
  sprite.setTextColor(GOLD, backColor);
  sprite.drawString(BANNER_SPACER2, xcursor, bannerY, CAR_FONT);
  xcursor += wSp2;

  // Time (bigger font 6)
  sprite.setTextFont(TIME_FONT);
  sprite.setTextColor(GOLD, backColor);
  sprite.drawString(timeStr, xcursor, bannerY, TIME_FONT);

  // restore datum/font
  sprite.setTextDatum(4);
  sprite.setTextFont(CAR_FONT);

  // push to screen
  sprite.pushSprite(0,10);
}

void setup() {
  // clamp
  CLAMP_MIN(needleThick,1); CLAMP_MIN(needleTail,1);
  CLAMP_MIN(tickThickMajor,1); CLAMP_MIN(tickThickMinor,1);
  CLAMP_MIN(r,10); CLAMP_MIN(ir,8);
  CLAMP_MIN(o1,2); CLAMP_MIN(o2,2); CLAMP_MIN(o3,3); CLAMP_MIN(o4,4);
  CLAMP_MIN(rimPlus,1); CLAMP_MIN(rimIn1,1); CLAMP_MIN(rimIn2,1);
  CLAMP_MIN(aux_r,16); CLAMP_MIN(aux_ir,14);

  pinMode(THROTTLE,INPUT_PULLUP); pinMode(BRAKE,INPUT_PULLUP);
  pinMode(LEFT,INPUT_PULLUP); pinMode(RIGHT,INPUT_PULLUP);
  pinMode(GEARUP,INPUT_PULLUP); pinMode(GEARDOWN,INPUT_PULLUP);
  pinMode(SHORT,INPUT_PULLUP); pinMode(LONG,INPUT_PULLUP);
  pinMode(HORN,INPUT_PULLUP); pinMode(BRIGHTNESS,INPUT_PULLUP);

  pinMode(left_pointer,OUTPUT); pinMode(right_pointer,OUTPUT);
  pinMode(head_lights,OUTPUT); pinMode(buzzer,OUTPUT);

  // TFT + sprite first (so textWidth is valid before WiFi time update)
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(backColor);

  sprite.createSprite(320,150);
  sprite.setSwapBytes(true);
  sprite.setTextDatum(4);
  sprite.setTextColor(WHITE,backColor);

  // backlight PWM
  ledcSetup(0, 10000, 8);
  ledcAttachPin(38, 0);
  ledcWrite(0, brightnesses[selectedBrightness]);

  // headlight PWM
  ledcSetup(1, 10000, 8);
  ledcAttachPin(head_lights, 1);

  // ---- WiFi auto-adjust base ----
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("P964-Dash");
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // Non-blocking config portal off by default; use autoConnect once on first boot
  wm.setConfigPortalTimeout(120);
  wm.autoConnect("P964-Setup");   // first-time setup if needed

  // One-liner TZ + start SNTP (Bern/Zurich)
  configTzTime(TZ_STRING, NTP_SERVER, "time.cloudflare.com", "time.google.com");

  // precompute arcs
  int a=120;
  for(int i=0;i<360;i++){
    x[i]  = ((r-o1)*cos(rad*a))+TACH_X;
    y[i]  = ((r-o1)*sin(rad*a))+TACH_Y;
    px[i] = ((r-o2)*cos(rad*a))+TACH_X;
    py[i] = ((r-o2)*sin(rad*a))+TACH_Y;
    lx[i] = ((r-o3)*cos(rad*a))+TACH_X;
    ly[i] = ((r-o3)*sin(rad*a))+TACH_Y;
    nxp[i] = ((r-o4)*cos(rad*a))+TACH_X;
    nyp[i] = ((r-o4)*sin(rad*a))+TACH_Y;

    x2[i]  = ((r-o1)*cos(rad*a))+SPEEDO_X;
    y2[i]  = ((r-o1)*sin(rad*a))+SPEEDO_Y;
    px2[i] = ((r-o2)*cos(rad*a))+SPEEDO_X;
    py2[i] = ((r-o2)*sin(rad*a))+SPEEDO_Y;
    lx2[i] = ((r-o3)*cos(rad*a))+SPEEDO_X;
    ly2[i] = ((r-o3)*sin(rad*a))+SPEEDO_Y;
    nx2p[i] = ((r-o4)*cos(rad*a))+SPEEDO_X;
    ny2p[i] = ((r-o4)*sin(rad*a))+SPEEDO_Y;

    a++; if(a==360) a=0;
  }

  // prime banner widths once we have sprite ready
  updateTimeStringOncePerSecond();
}

void loop() {
  if(digitalRead(SHORT)==0)      lights=1;
  else if(digitalRead(LONG)==0)  lights=2;
  else                           lights=0;
  ledcWrite(1, lights*4);

  digitalWrite(buzzer,digitalRead(HORN)==0);
  braking=!(digitalRead(BRAKE));

  // Brightness button short-press handling (existing)
  if(digitalRead(BRIGHTNESS)==0){
    if(debB==0){
      debB=1;
      selectedBrightness=(selectedBrightness+1)%5;
      ledcWrite(0,brightnesses[selectedBrightness]);
    }
  } else debB=0;

  if(digitalRead(GEARUP)==0){
    if(deb1==0){deb1=1;
      if(selectedGear<7) selectedGear++;
      if(speedAngle>10) speedAngle-=4;
    }
  } else deb1=0;

  if(digitalRead(GEARDOWN)==0){
    if(deb2==0){deb2=1;
      if(selectedGear>0) selectedGear--;
      if(speedAngle>10) speedAngle-=4;
    }
  } else deb2=0;

  if(digitalRead(LEFT)==0){
    if(millis()>currentTimeL+blinkPeriod){
      leftPointer=!leftPointer;
      digitalWrite(left_pointer,leftPointer);
      currentTimeL=millis();
    }
  } else { leftPointer=0; digitalWrite(left_pointer,leftPointer); }

  if(digitalRead(RIGHT)==0){
    if(millis()>currentTimeR+blinkPeriod){
      rightPointer=!rightPointer;
      digitalWrite(right_pointer,rightPointer);
      currentTimeR=millis();
    }
  } else { rightPointer=0; digitalWrite(right_pointer,rightPointer); }

  if(braking && speedAngle>4) speedAngle-=4;
  if(speedAngle<0) speedAngle=0;

  // Auto-adjust WiFi (reconnect / optional portal)
  ensureWifi();

  // Update time/date and recompute banner width
  updateTimeStringOncePerSecond();

  // animate banner (use measured full width)
  bannerX -= 2;
  int tw = bannerFullWidth > 0 ? bannerFullWidth : sprite.textWidth(BANNER_TEXT, CAR_FONT);
  if (bannerX < -tw) bannerX = 320;

  draw();

  // simple accel/decay model
  if(digitalRead(THROTTLE)==0 && speedAngle<min(gearMaxSpeed[selectedGear],300)){
    speedAngle = speedAngle + 2 - (0.24f*selectedGear);
  }
  if(digitalRead(THROTTLE)==1 && speedAngle>0){
    speedAngle--;
  }
  if(digitalRead(THROTTLE)==0 && rpmAngle<75){
    rpmAngle = rpmAngle + 1 - (0.1f*selectedGear);
  }
  if(digitalRead(THROTTLE)==1 && rpmAngle>0){
    rpmAngle = (rpmAngle>=3) ? rpmAngle-3 : 0;
  }
}

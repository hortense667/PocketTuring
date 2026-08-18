#include <M5Unified.h>

#define W 116
#define H 116
#define SCALE 4.0

uint16_t u[W][H];
uint16_t v[W][H];
uint16_t next_u[W][H];
uint16_t next_v[W][H];

int preset_idx = 0;
bool is_manual = false; 
char popup_msg[32] = ""; // ポップアップ表示用メッセージ

float presets_f[] = {0.029, 0.022, 0.030, 0.014, 0.035, 0.026, 0.062, 0.037};
float presets_k[] = {0.057, 0.051, 0.062, 0.045, 0.056, 0.052, 0.061, 0.060};
const char* preset_names[] = {"MAZES", "STRIPES", "SPOTS", "WAVES", "HOLES", "MIXED", "U-SKATE", "WORMS"};

float current_f;
float current_k;

int color_mode = 0;
M5Canvas canvas(&M5.Display);

uint32_t pattern_start_time = 0;
uint32_t preset_show_time = 0;
bool show_preset = true;

const float TO_FLT = 0.00001525902189f;
const float K_ADJ = 0.2f * TO_FLT;
const float K_DIAG = 0.05f * TO_FLT;

TaskHandle_t Task0;
SemaphoreHandle_t sem_calc_start;
SemaphoreHandle_t sem_calc_done;
SemaphoreHandle_t sem_update_start;
SemaphoreHandle_t sem_update_done;

void Task0Code(void * pvParameters) {
    for(;;) {
        xSemaphoreTake(sem_calc_start, portMAX_DELAY);
        for (int x = 1; x < W / 2; x++) {
            for (int y = 1; y < H - 1; y++) {
                float a = u[x][y] * TO_FLT;
                float b = v[x][y] * TO_FLT;

                uint32_t sumA_adj = u[x][y-1] + u[x][y+1] + u[x-1][y] + u[x+1][y];
                uint32_t sumA_diag = u[x-1][y-1] + u[x-1][y+1] + u[x+1][y-1] + u[x+1][y+1];
                float lapA = sumA_adj * K_ADJ + sumA_diag * K_DIAG - a;

                uint32_t sumB_adj = v[x][y-1] + v[x][y+1] + v[x-1][y] + v[x+1][y];
                uint32_t sumB_diag = v[x-1][y-1] + v[x-1][y+1] + v[x+1][y-1] + v[x+1][y+1];
                float lapB = sumB_adj * K_ADJ + sumB_diag * K_DIAG - b;

                float abb = a * b * b;

                float next_a = a + lapA - abb + current_f * (1.0f - a);
                float next_b = b + 0.5f * lapB + abb - (current_k + current_f) * b;

                if (next_a < 0.0f) next_a = 0.0f; else if (next_a > 1.0f) next_a = 1.0f;
                if (next_b < 0.0f) next_b = 0.0f; else if (next_b > 1.0f) next_b = 1.0f;

                next_u[x][y] = (uint16_t)(next_a * 65535.0f);
                next_v[x][y] = (uint16_t)(next_b * 65535.0f);
            }
        }
        xSemaphoreGive(sem_calc_done);

        xSemaphoreTake(sem_update_start, portMAX_DELAY);
        for (int x = 1; x < W / 2; x++) {
            for (int y = 1; y < H - 1; y++) {
                u[x][y] = next_u[x][y];
                v[x][y] = next_v[x][y];
            }
        }
        xSemaphoreGive(sem_update_done);
    }
}

// --- 新機能：反応物質の投下 ---
void dropSeed() {
    // 中央に大きな塊を投下
    for (int x = W / 2 - 8; x < W / 2 + 8; x++) {
        for (int y = H / 2 - 8; y < H / 2 + 8; y++) {
            u[x][y] = 32768; // 0.5f
            v[x][y] = 32768; // 0.5f
        }
    }
    // 周囲に飛沫を散らす
    for(int i = 0; i < 40; i++) {
        int cx = W / 2 + random(-25, 25);
        int cy = H / 2 + random(-25, 25);
        int size = random(2, 5); 
        for(int x = cx; x < cx + size; x++) {
            for(int y = cy; y < cy + size; y++) {
                if(x > 0 && x < W - 1 && y > 0 && y < H - 1) {
                    u[x][y] = 32768; 
                    float v_val = 0.25f + random(-5, 5) / 100.0f;
                    v[x][y] = (uint16_t)(v_val * 65535.0f);
                }
            }
        }
    }
}

void resetPattern() {
    // 画面を真っさらにリセット
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            u[x][y] = 65535;
            v[x][y] = 0;
        }
    }
    
    // 最初の物質投下
    dropSeed();
    
    if (!is_manual) {
        current_f = presets_f[preset_idx];
        current_k = presets_k[preset_idx];
        sprintf(popup_msg, " %s ", preset_names[preset_idx]);
    } else {
        sprintf(popup_msg, " MANUAL TUNE ");
    }
    
    pattern_start_time = millis();
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(128);
    
    canvas.createSprite(W, H);
    
    sem_calc_start = xSemaphoreCreateBinary();
    sem_calc_done = xSemaphoreCreateBinary();
    sem_update_start = xSemaphoreCreateBinary();
    sem_update_done = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(Task0Code, "Task0", 4096, NULL, 1, &Task0, 0);
    
    resetPattern();
    preset_show_time = millis();
}

void updateGS() {
    for(int step = 0; step < 14; step++) {
        for (int i = 1; i < W - 1; i++) {
            u[i][0] = u[i][H-2]; u[i][H-1] = u[i][1];
            v[i][0] = v[i][H-2]; v[i][H-1] = v[i][1];
            u[0][i] = u[W-2][i]; u[W-1][i] = u[1][i];
            v[0][i] = v[W-2][i]; v[W-1][i] = v[1][i];
        }
        u[0][0] = u[W-2][H-2];       u[W-1][0] = u[1][H-2];
        u[0][H-1] = u[W-2][1];       u[W-1][H-1] = u[1][1];
        v[0][0] = v[W-2][H-2];       v[W-1][0] = v[1][H-2];
        v[0][H-1] = v[W-2][1];       v[W-1][H-1] = v[1][1];

        xSemaphoreGive(sem_calc_start);

        for (int x = W / 2; x < W - 1; x++) {
            for (int y = 1; y < H - 1; y++) {
                float a = u[x][y] * TO_FLT;
                float b = v[x][y] * TO_FLT;

                uint32_t sumA_adj = u[x][y-1] + u[x][y+1] + u[x-1][y] + u[x+1][y];
                uint32_t sumA_diag = u[x-1][y-1] + u[x-1][y+1] + u[x+1][y-1] + u[x+1][y+1];
                float lapA = sumA_adj * K_ADJ + sumA_diag * K_DIAG - a;

                uint32_t sumB_adj = v[x][y-1] + v[x][y+1] + v[x-1][y] + v[x+1][y];
                uint32_t sumB_diag = v[x-1][y-1] + v[x-1][y+1] + v[x+1][y-1] + v[x+1][y+1];
                float lapB = sumB_adj * K_ADJ + sumB_diag * K_DIAG - b;

                float abb = a * b * b;

                float next_a = a + lapA - abb + current_f * (1.0f - a);
                float next_b = b + 0.5f * lapB + abb - (current_k + current_f) * b;

                if (next_a < 0.0f) next_a = 0.0f; else if (next_a > 1.0f) next_a = 1.0f;
                if (next_b < 0.0f) next_b = 0.0f; else if (next_b > 1.0f) next_b = 1.0f;

                next_u[x][y] = (uint16_t)(next_a * 65535.0f);
                next_v[x][y] = (uint16_t)(next_b * 65535.0f);
            }
        }
        
        xSemaphoreTake(sem_calc_done, portMAX_DELAY);
        xSemaphoreGive(sem_update_start);

        for (int x = W / 2; x < W - 1; x++) {
            for (int y = 1; y < H - 1; y++) {
                u[x][y] = next_u[x][y];
                v[x][y] = next_v[x][y];
            }
        }
        
        xSemaphoreTake(sem_update_done, portMAX_DELAY);
    }
}

void drawCanvas() {
    uint16_t* buf = (uint16_t*)canvas.getBuffer();
    
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int32_t diff = (int32_t)u[x][y] - (int32_t)v[x][y];
            int c = (abs(diff) * 255) / 65535;
            if (c > 255) c = 255;
            
            uint8_t r, g, b;
            switch (color_mode) {
                case 0: r = c;       g = c;       b = c;       break; 
                case 1: r = c;       g = 255 - c; b = 255;     break; 
                case 2: r = 255;     g = c;       b = 0;       break; 
                case 3: r = 0;       g = 255;     b = c;       break; 
                case 4: r = c;       g = 0;       b = 255;     break; 
                case 5: r = 255;     g = 255 - c; b = c;       break; 
                case 6: r = 255 - c; g = c;       b = 128;     break; 
                case 7: r = c / 2;   g = c;       b = 255 - c; break; 
                default: r=c; g=c; b=c; break;
            }
            
            uint16_t color = M5.Display.color565(r, g, b);
            *buf++ = (color >> 8) | (color << 8);
        }
    }
    canvas.pushRotateZoom(233, 233, 0, SCALE, SCALE);
}

void drawUI() {
    uint16_t c_white = M5.Display.color565(255, 255, 255);
    uint16_t c_black = M5.Display.color565(0, 0, 0);
    uint16_t c_accent = M5.Display.color565(220, 40, 60);

    M5.Display.setTextDatum(middle_center);

    int elapsed_sec = (millis() - pattern_start_time) / 1000;
    char info_buf[64];
    sprintf(info_buf, " F:%.3f  K:%.3f  T:%ds ", current_f, current_k, elapsed_sec);
    
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(c_white, c_black);
    M5.Display.drawString(info_buf, 233, 415); 

    if (show_preset) {
        if (millis() - preset_show_time < 2000) {
            M5.Display.setTextSize(3);
            M5.Display.setTextColor(c_white, c_accent);
            M5.Display.drawString(popup_msg, 233, 110);
        } else {
            show_preset = false;
        }
    }
}

// タッチ座標の直前値を保存する変数
int prev_y_right = -1;
int prev_y_left = -1;

void loop() {
    M5.update();

    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
        float dx = t.x - 233;
        float dy = t.y - 233;
        float r = sqrt(dx*dx + dy*dy);

        // 【機能1】フチなぞりで相対的に数値を上下させる
        if (r > 130) {
            if (!is_manual) {
                is_manual = true;
                sprintf(popup_msg, " MANUAL TUNE ");
            }
            preset_show_time = millis();
            show_preset = true;
            
            if (t.x >= 233) {
                // 右半分：Feed値 (0.010 〜 0.100)
                if (prev_y_right == -1) prev_y_right = t.y;
                else {
                    float delta = (prev_y_right - t.y) * 0.00015f; // 上にスワイプでプラス
                    current_f = constrain(current_f + delta, 0.010f, 0.100f);
                    prev_y_right = t.y;
                }
                prev_y_left = -1;
            } else {
                // 左半分：Kill値 (0.030 〜 0.070)
                if (prev_y_left == -1) prev_y_left = t.y;
                else {
                    float delta = (prev_y_left - t.y) * 0.00015f; 
                    current_k = constrain(current_k + delta, 0.030f, 0.070f);
                    prev_y_left = t.y;
                }
                prev_y_right = -1;
            }
        } 
        // 【機能2】中央タップで反応物質を投下
        else if (r < 80 && t.wasPressed()) {
            dropSeed();
            sprintf(popup_msg, " DROP SEED! ");
            preset_show_time = millis();
            show_preset = true;
        }
    } else {
        // 指が離れたらリセット
        prev_y_right = -1;
        prev_y_left = -1;
    }

    // 【青ボタン】プリセットの切り替え (手動モードを解除)
    if (M5.BtnA.wasPressed()) {
        is_manual = false;
        preset_idx = (preset_idx + 1) % 8;
        resetPattern();
        
        preset_show_time = millis();
        show_preset = true;
    }

    // 【オレンジボタン】色の切り替え
    if (M5.BtnB.wasPressed() || M5.BtnC.wasPressed() || M5.BtnPWR.wasPressed()) {
        color_mode = (color_mode + 1) % 8;
    }

    updateGS();
    drawCanvas();
    drawUI();
}
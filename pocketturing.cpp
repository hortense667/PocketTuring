#include <M5Unified.h>
#include <FFat.h> 

#define W 116
#define H 116
#define SCALE 4.0

uint16_t u[W][H];
uint16_t v[W][H];
uint16_t next_u[W][H];
uint16_t next_v[W][H];

int app_mode = 0; 
int menu_cursor = 0;
int gallery_idx = 0;
int total_saves = 0;

uint32_t btn_blue_press_time = 0;
uint32_t btn_orange_press_time = 0;

bool menu_needs_redraw = true; 
bool orange_action_flag = false; 
bool gallery_blue_ready = false; 
bool gallery_orange_ready = false; // 【追加】オレンジボタン用フラグ

int preset_idx = 0;
bool is_manual = false; 
char popup_msg[32] = "";

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

void saveStateToFile(const char* path) {
    File f = FFat.open(path, FILE_WRITE);
    if(f){
        f.write((uint8_t*)&color_mode, sizeof(color_mode));
        f.write((uint8_t*)&current_f, sizeof(current_f));
        f.write((uint8_t*)&current_k, sizeof(current_k));
        f.write((uint8_t*)u, sizeof(u));
        f.write((uint8_t*)v, sizeof(v));
        f.close();
    }
}

void loadStateFromFile(const char* path) {
    File f = FFat.open(path, FILE_READ);
    if(f){
        f.read((uint8_t*)&color_mode, sizeof(color_mode));
        f.read((uint8_t*)&current_f, sizeof(current_f));
        f.read((uint8_t*)&current_k, sizeof(current_k));
        f.read((uint8_t*)u, sizeof(u));
        f.read((uint8_t*)v, sizeof(v));
        f.close();
    }
}

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

void dropSeed() {
    for (int x = W / 2 - 8; x < W / 2 + 8; x++) {
        for (int y = H / 2 - 8; y < H / 2 + 8; y++) {
            u[x][y] = 32768; 
            v[x][y] = 32768; 
        }
    }
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
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < H; y++) {
            u[x][y] = 65535;
            v[x][y] = 0;
        }
    }
    dropSeed();
    
    if (!is_manual) {
        current_f = presets_f[preset_idx];
        current_k = presets_k[preset_idx];
        sprintf(popup_msg, " %s ", preset_names[preset_idx]);
    } else {
        sprintf(popup_msg, " MANUAL TUNE ");
    }
    
    pattern_start_time = millis();
    preset_show_time = millis();
    show_preset = true;
}

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setBrightness(128);
    canvas.createSprite(W, H);
    
    FFat.begin(true);
    total_saves = 0;
    while (true) {
        char path[32];
        sprintf(path, "/save_%03d.dat", total_saves);
        if (FFat.exists(path)) {
            total_saves++;
        } else {
            break;
        }
    }
    
    sem_calc_start = xSemaphoreCreateBinary();
    sem_calc_done = xSemaphoreCreateBinary();
    sem_update_start = xSemaphoreCreateBinary();
    sem_update_done = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(Task0Code, "Task0", 4096, NULL, 1, &Task0, 0);
    
    resetPattern();
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

void drawMenu() {
    M5.Display.fillCircle(233, 233, 140, M5.Display.color565(15, 15, 15));
    M5.Display.drawCircle(233, 233, 140, M5.Display.color565(80, 80, 80));

    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(M5.Display.color565(255, 255, 255));
    M5.Display.drawString("- MENU -", 233, 130);

    const char* items[] = {"SAVE CURRENT", "GALLERY", "CLEAR ALL", "EXIT"};
    M5.Display.setTextSize(2);
    
    for (int i = 0; i < 4; i++) {
        int y = 180 + i * 40;
        if (i == menu_cursor) {
            M5.Display.setTextColor(M5.Display.color565(220, 40, 60));
            char buf[32];
            sprintf(buf, "> %s <", items[i]);
            M5.Display.drawString(buf, 233, y);
        } else {
            M5.Display.setTextColor(M5.Display.color565(180, 180, 180));
            M5.Display.drawString(items[i], 233, y);
        }
    }
}

void drawGalleryUI() {
    uint16_t c_white = M5.Display.color565(255, 255, 255);
    uint16_t c_black = M5.Display.color565(0, 0, 0);
    uint16_t c_blue = M5.Display.color565(60, 150, 255);
    uint16_t c_accent = M5.Display.color565(220, 40, 60);

    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(c_white, c_black);
    
    char buf[32];
    sprintf(buf, " GALLERY %d/%d ", gallery_idx + 1, total_saves);
    M5.Display.drawString(buf, 233, 415);

    // 【追加】操作ガイドの表示を更新
    M5.Display.setTextColor(c_white, c_blue);
    M5.Display.drawString(" HOLD BLUE: LOAD ", 233, 85);
    M5.Display.setTextColor(c_white, c_accent);
    M5.Display.drawString(" HOLD ORG: DEL ", 233, 115);

    // 【追加】ギャラリーでもポップアップ（DELETEDなど）を表示
    if (show_preset) {
        if (millis() - preset_show_time < 2000) {
            M5.Display.setTextSize(3);
            M5.Display.setTextColor(c_white, c_accent);
            M5.Display.drawString(popup_msg, 233, 180);
        } else {
            show_preset = false;
        }
    }
}

int prev_y_right = -1;
int prev_y_left = -1;

void loop() {
    M5.update();
    
    bool orange_pressed  = M5.BtnA.wasPressed();
    bool orange_held     = M5.BtnA.isPressed();
    bool orange_released = M5.BtnA.wasReleased();

    bool blue_pressed  = M5.BtnB.wasPressed() || M5.BtnC.wasPressed() || M5.BtnPWR.wasPressed();
    bool blue_held     = M5.BtnB.isPressed() || M5.BtnC.isPressed() || M5.BtnPWR.isPressed();
    bool blue_released = M5.BtnB.wasReleased() || M5.BtnC.wasReleased() || M5.BtnPWR.wasReleased();

    if (app_mode == 0) {
        // --- 0: シミュレーションモード ---

        if (orange_held && blue_pressed) {
            app_mode = 1;
            menu_cursor = 0;
            menu_needs_redraw = true; 
            orange_action_flag = true; 
            saveStateToFile("/backup.dat"); 
            return; 
        }

        auto t = M5.Touch.getDetail();
        if (t.isPressed()) {
            float dx = t.x - 233;
            float dy = t.y - 233;
            float r = sqrt(dx*dx + dy*dy);

            if (r > 130) {
                if (!is_manual) {
                    is_manual = true;
                    sprintf(popup_msg, " MANUAL TUNE ");
                    preset_show_time = millis();
                    show_preset = true;
                }
                
                if (t.x >= 233) {
                    if (prev_y_right == -1) prev_y_right = t.y;
                    else {
                        float delta = (prev_y_right - t.y) * 0.00015f; 
                        current_f = constrain(current_f + delta, 0.010f, 0.100f);
                        prev_y_right = t.y;
                    }
                    prev_y_left = -1;
                } else {
                    if (prev_y_left == -1) prev_y_left = t.y;
                    else {
                        float delta = (prev_y_left - t.y) * 0.00015f; 
                        current_k = constrain(current_k + delta, 0.030f, 0.070f);
                        prev_y_left = t.y;
                    }
                    prev_y_right = -1;
                }
            } 
            else if (r < 80 && t.wasPressed()) {
                dropSeed();
                sprintf(popup_msg, " DROP SEED! ");
                preset_show_time = millis();
                show_preset = true;
            }
        } else {
            prev_y_right = -1;
            prev_y_left = -1;
        }

        if (orange_released) {
            if (orange_action_flag) {
                orange_action_flag = false;
            } else {
                preset_idx = (preset_idx + 1) % 8;
                is_manual = false;
                resetPattern();
            }
        }

        if (!orange_held && blue_pressed) {
            color_mode = (color_mode + 1) % 8;
        }

        updateGS();
        drawCanvas();
        drawUI();
    } 
    else if (app_mode == 1) {
        // --- 1: メニューモード ---
        
        if (orange_pressed) { 
            menu_cursor = (menu_cursor + 1) % 4; 
            menu_needs_redraw = true;
        }
        
        if (blue_pressed) { 
            if (menu_cursor == 0) { // SAVE
                char path[32];
                sprintf(path, "/save_%03d.dat", total_saves);
                saveStateToFile(path);
                total_saves++;
                
                sprintf(popup_msg, " SAVED! ");
                preset_show_time = millis();
                show_preset = true;
                app_mode = 0;
            } 
            else if (menu_cursor == 1) { // GALLERY
                if (total_saves > 0) {
                    app_mode = 2;
                    gallery_idx = 0;
                    char path[32];
                    sprintf(path, "/save_%03d.dat", gallery_idx);
                    loadStateFromFile(path);
                    gallery_blue_ready = false; 
                    gallery_orange_ready = false;
                } else {
                    sprintf(popup_msg, " NO DATA ");
                    preset_show_time = millis();
                    show_preset = true;
                    app_mode = 0;
                }
            } 
            else if (menu_cursor == 2) { // CLEAR ALL
                for(int i = 0; i < total_saves; i++){
                    char path[32];
                    sprintf(path, "/save_%03d.dat", i);
                    FFat.remove(path);
                }
                total_saves = 0;
                
                sprintf(popup_msg, " CLEARED! ");
                preset_show_time = millis();
                show_preset = true;
                app_mode = 0;
            } 
            else if (menu_cursor == 3) { // EXIT
                loadStateFromFile("/backup.dat"); 
                app_mode = 0;
            }
        }
        
        if (menu_needs_redraw) {
            drawCanvas(); 
            drawMenu();
            menu_needs_redraw = false;
        }
    }
    else if (app_mode == 2) {
        // --- 2: ギャラリーモード ---
        
        // 【オレンジボタンの処理】
        if (orange_pressed) {
            btn_orange_press_time = millis();
            gallery_orange_ready = true;
            gallery_blue_ready = false; // 誤爆防止
        }

        if (orange_released) {
            if (gallery_orange_ready) {
                gallery_orange_ready = false;
                if (millis() - btn_orange_press_time > 500) {
                    // 【オレンジ長押し】現在見ている画像を削除する
                    char path_del[32];
                    sprintf(path_del, "/save_%03d.dat", gallery_idx);
                    FFat.remove(path_del);

                    // 以降のファイルをリネームして隙間を詰める
                    for (int i = gallery_idx + 1; i < total_saves; i++) {
                        char path_old[32], path_new[32];
                        sprintf(path_old, "/save_%03d.dat", i);
                        sprintf(path_new, "/save_%03d.dat", i - 1);
                        FFat.rename(path_old, path_new);
                    }
                    total_saves--;

                    if (total_saves == 0) {
                        // 全部消えたらメニューに戻る
                        sprintf(popup_msg, " EMPTY! ");
                        preset_show_time = millis();
                        show_preset = true;
                        loadStateFromFile("/backup.dat");
                        app_mode = 1;
                        menu_needs_redraw = true;
                    } else {
                        // まだあるなら番号を調整して再表示
                        if (gallery_idx >= total_saves) gallery_idx = 0;
                        char path_load[32];
                        sprintf(path_load, "/save_%03d.dat", gallery_idx);
                        loadStateFromFile(path_load);
                        
                        sprintf(popup_msg, " DELETED! ");
                        preset_show_time = millis();
                        show_preset = true;
                    }
                } else {
                    // 【オレンジ短押し】次の保存画像へ送る
                    gallery_idx = (gallery_idx + 1) % total_saves;
                    char path[32];
                    sprintf(path, "/save_%03d.dat", gallery_idx);
                    loadStateFromFile(path);
                }
            }
        }
        
        // 【青ボタンの処理】
        if (blue_pressed) {
            btn_blue_press_time = millis(); 
            gallery_blue_ready = true; 
            gallery_orange_ready = false; // 誤爆防止
        }
        
        if (blue_released) {
            if (gallery_blue_ready) { 
                gallery_blue_ready = false;
                if (millis() - btn_blue_press_time > 500) {
                    // 【青長押し】ロードして培養再開
                    app_mode = 0;
                    is_manual = true; 
                    sprintf(popup_msg, " LOADED! ");
                    preset_show_time = millis();
                    show_preset = true;
                } else {
                    // 【青短押し】閲覧終了してメニューへ
                    loadStateFromFile("/backup.dat");
                    app_mode = 1;
                    menu_needs_redraw = true;
                }
            }
        }
        
        drawCanvas();
        drawGalleryUI();
    }
}
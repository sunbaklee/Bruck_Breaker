#include <GL/glut.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#pragma warning(disable:4996)

#define PI 3.14159265f

// --- 게임 상태 및 모드 관리 ---
typedef enum { MENU, INSTRUCTION, PLAYING, STAGE_CLEAR, ALL_CLEAR } GameState;
GameState currentState = MENU;

bool isEndlessMode = false;
int currentStage = 1;
int maxStage = 3;

// 시간 측정 및 기록용 변수
int startTime = 0;
int accumulatedTime = 0;
int finalClearTime = 0;

float bestTimeSeconds = 9999.0f;
int bestWave = 0;

// 필살기(Z키) 쿨타임 및 사용 횟수 관련 변수
int lastSkillTime = -10000;
int skillUseCount = 0; // 스킬 사용 횟수 추적

// --- 전역 변수 ---
float blockRotX = 15.0f;
float blockRotY = -15.0f;

int lastMouseX, lastMouseY;
bool isDragging = false;

float wallRotZ = 0.0f;
bool activeBricks[3][3][3];

// 공 속도 및 물리엔진
float ballX = 0.0f, ballY = -4.0f;
float ballSpeedX = 0.05f, ballSpeedY = 0.12f;
float gravity = 0.002f;
float maxSpeed = 0.25f;

// 우주 배경 파티클
#define NUM_STARS 200
float stars[NUM_STARS][3];

// --- 최고 기록 파일 입출력 ---
void loadHighScore() {
    FILE* fp = fopen("score.txt", "r");
    if (fp != NULL) {
        if (fscanf(fp, "%f %d", &bestTimeSeconds, &bestWave) != 2) {
            bestTimeSeconds = 9999.0f;
            bestWave = 0;
        }
        fclose(fp);
    }
}

void saveHighScore() {
    FILE* fp = fopen("score.txt", "w");
    if (fp != NULL) {
        fprintf(fp, "%f %d", bestTimeSeconds, bestWave);
        fclose(fp);
    }
}

// --- 스테이지 초기화 ---
void initStage(int stage) {
    ballX = 0.0f; ballY = -4.0f;
    ballSpeedX = 0.05f; ballSpeedY = 0.12f;
    blockRotX = 15.0f; blockRotY = -15.0f;

    lastSkillTime = -10000;
    skillUseCount = 0; // 스테이지가 시작될 때마다 스킬 사용 횟수 초기화 (1회 정확도 보장)

    for (int x = 0; x < 3; x++) {
        for (int y = 0; y < 3; y++) {
            for (int z = 0; z < 3; z++) {
                if (isEndlessMode) {
                    if (x == 1 && y == 1 && z == 1) {
                        activeBricks[x][y][z] = true;
                    }
                    else {
                        activeBricks[x][y][z] = (rand() % 100 < 75);
                    }
                }
                else {
                    if (stage == 1) {
                        activeBricks[x][y][z] = (x == 1 && y == 1) || (y == 1 && z == 1) || (x == 1 && z == 1);
                    }
                    else if (stage == 2) {
                        activeBricks[x][y][z] = !((x != 1) && (y != 1) && (z != 1));
                    }
                    else {
                        activeBricks[x][y][z] = true;
                    }
                }
            }
        }
    }
    activeBricks[1][1][1] = true;
}

// --- 초기화 함수 ---
void init() {
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat lightPos[] = { 10.0f, 10.0f, 10.0f, 1.0f };
    GLfloat lightDiffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
    glEnable(GL_COLOR_MATERIAL);

    for (int i = 0; i < NUM_STARS; i++) {
        stars[i][0] = (rand() % 400 - 200) / 10.0f;
        stars[i][1] = (rand() % 400 - 200) / 10.0f;
        stars[i][2] = (rand() % 400 - 200) / 10.0f - 10.0f;
    }

    loadHighScore();
}

// --- 우주 배경 및 오브젝트 렌더링 ---
void drawAndMoveStars() {
    glDisable(GL_LIGHTING);
    glPointSize(2.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i < NUM_STARS; i++) {
        float brightness = (stars[i][2] + 30.0f) / 40.0f;
        glColor3f(brightness * 0.8f, brightness * 0.9f, brightness);
        glVertex3f(stars[i][0], stars[i][1], stars[i][2]);
        stars[i][2] += 0.05f;
        if (stars[i][2] > 10.0f) {
            stars[i][2] = -30.0f;
            stars[i][0] = (rand() % 400 - 200) / 10.0f;
            stars[i][1] = (rand() % 400 - 200) / 10.0f;
        }
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawOctagonWall() {
    glPushMatrix();
    glRotatef(wallRotZ, 0.0f, 0.0f, 1.0f);

    float timeValue = glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    float r = (sin(timeValue * 2.0f) + 1.0f) * 0.5f;
    float g = (cos(timeValue * 1.5f) + 1.0f) * 0.5f;
    float b = 1.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LIGHTING);

    float radius = 7.5f;
    for (int thickness = 1; thickness <= 3; thickness++) {
        glLineWidth(9.0f - thickness * 2.5f);
        glColor4f(r, g, b, 0.2f * thickness);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 8; i++) {
            float theta = 2.0f * PI * (float)i / 8.0f;
            glVertex3f(radius * cos(theta), radius * sin(theta), 0.0f);
        }
        glEnd();
    }
    glLineWidth(2.0f);
    glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 8; i++) {
        float theta = 2.0f * PI * (float)i / 8.0f;
        glVertex3f(radius * cos(theta), radius * sin(theta), 0.0f);
    }
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glPopMatrix();
}

bool checkCollision() {
    float spacing = 1.1f;
    float radX = blockRotX * PI / 180.0f;
    float radY = blockRotY * PI / 180.0f;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                if (!activeBricks[x + 1][y + 1][z + 1]) continue;

                float lx = x * spacing;
                float ly = y * spacing;
                float lz = z * spacing;

                float rx1 = lx * cos(radY) + lz * sin(radY);
                float ry1 = ly;
                float rz1 = -lx * sin(radY) + lz * cos(radY);

                float rx2 = rx1;
                float ry2 = ry1 * cos(radX) - rz1 * sin(radX);
                float rz2 = ry1 * sin(radX) + rz1 * cos(radX);

                float dx = ballX - rx2;
                float dy = ballY - ry2;
                float dz = 0.0f - rz2;
                float dist = sqrt(dx * dx + dy * dy + dz * dz);

                if (dist < 0.9f) {
                    activeBricks[x + 1][y + 1][z + 1] = false;

                    float currentSpeed = sqrt(ballSpeedX * ballSpeedX + ballSpeedY * ballSpeedY);
                    if (currentSpeed > 0.2f) {
                        ballSpeedX *= -0.5f;
                        ballSpeedY *= -0.5f;
                    }
                    else {
                        ballSpeedX *= -1.02f;
                        ballSpeedY *= -1.02f;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

void checkStageClear() {
    if (!activeBricks[1][1][1]) {
        accumulatedTime += (glutGet(GLUT_ELAPSED_TIME) - startTime);

        if (isEndlessMode) {
            currentStage++;
            if (currentStage > bestWave) {
                bestWave = currentStage;
                saveHighScore();
            }
            initStage(currentStage);
            startTime = glutGet(GLUT_ELAPSED_TIME);
        }
        else {
            if (currentStage < maxStage) {
                currentState = STAGE_CLEAR;
            }
            else {
                currentState = ALL_CLEAR;
                finalClearTime = accumulatedTime;

                float finalTimeSeconds = finalClearTime / 1000.0f;
                if (finalTimeSeconds < bestTimeSeconds) {
                    bestTimeSeconds = finalTimeSeconds;
                    saveHighScore();
                }
            }
        }
    }
}

void draw3x3x3Bricks() {
    glPushMatrix();
    glRotatef(blockRotX, 1.0f, 0.0f, 0.0f);
    glRotatef(blockRotY, 0.0f, 1.0f, 0.0f);

    float spacing = 1.1f;

    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            for (int z = -1; z <= 1; z++) {
                if (!activeBricks[x + 1][y + 1][z + 1]) continue;

                glPushMatrix();
                glTranslatef(x * spacing, y * spacing, z * spacing);

                if (x == 0 && y == 0 && z == 0) glColor3f(0.2f, 0.4f, 1.0f);
                else glColor3f(0.3f, 0.3f, 0.4f);

                glutSolidCube(1.0f);
                glColor3f(0.5f, 0.8f, 1.0f);
                glutWireCube(1.01f);
                glPopMatrix();
            }
        }
    }
    glPopMatrix();
}

void draw2DBall(float radius) {
    glDisable(GL_LIGHTING);
    glColor3f(1.0f, 0.5f, 0.0f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= 30; i++) {
        float angle = 2.0f * PI * (float)i / 30.0f;
        glVertex2f(radius * cos(angle), radius * sin(angle));
    }
    glEnd();
    glEnable(GL_LIGHTING);
}

void drawAndMoveBall() {
    if (currentState != PLAYING) return;

    ballY += ballSpeedY;
    ballX += ballSpeedX;
    ballSpeedY += gravity;

    float distFromCenter = sqrt(ballX * ballX + ballY * ballY);
    float wallRadius = 7.0f;

    if (distFromCenter > wallRadius) {
        float nx = ballX / distFromCenter;
        float ny = ballY / distFromCenter;
        float dot = ballSpeedX * nx + ballSpeedY * ny;
        ballSpeedX = ballSpeedX - 2 * dot * nx;
        ballSpeedY = ballSpeedY - 2 * dot * ny;

        ballSpeedX *= 1.02f;
        ballSpeedY *= 1.02f;
        ballX = nx * wallRadius;
        ballY = ny * wallRadius;
    }

    checkCollision();
    checkStageClear();

    float currentSpeed = sqrt(ballSpeedX * ballSpeedX + ballSpeedY * ballSpeedY);
    if (currentSpeed > maxSpeed) {
        ballSpeedX *= 0.95f;
        ballSpeedY *= 0.95f;
    }

    glPushMatrix();
    glTranslatef(ballX, ballY, 0.0f);
    draw2DBall(0.4f);
    glPopMatrix();
}

// --- 텍스트 렌더링 헬퍼 함수 ---
void renderText(float x, float y, void* font, const char* string, float r, float g, float b) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (const char* c = string; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

// --- 2D UI 및 상태별 화면 렌더링 ---
void drawUI() {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);
    gluOrtho2D(0, w, 0, h);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    char textBuffer[128];

    if (currentState == MENU) {
        renderText(w / 2 - 170, h / 2 + 80, GLUT_BITMAP_TIMES_ROMAN_24, "OCTAGON REVERSE GRAVITY", 0.0f, 1.0f, 1.0f);

        if (bestTimeSeconds < 9999.0f) {
            sprintf(textBuffer, "Story Best Time: %.3f sec", bestTimeSeconds);
            renderText(w / 2 - 100, h / 2 + 30, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 1.0f, 0.0f);
        }
        if (bestWave > 0) {
            sprintf(textBuffer, "Endless Best Wave: %d", bestWave);
            renderText(w / 2 - 100, h / 2, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 0.5f, 0.0f);
        }

        renderText(w / 2 - 100, h / 2 - 40, GLUT_BITMAP_HELVETICA_18, "Press '1' - Story Mode", 1.0f, 1.0f, 1.0f);
        renderText(w / 2 - 100, h / 2 - 70, GLUT_BITMAP_HELVETICA_18, "Press '2' - Endless Mode", 1.0f, 1.0f, 1.0f);
        renderText(w / 2 - 100, h / 2 - 100, GLUT_BITMAP_HELVETICA_18, "Press '3' - How to Play", 0.3f, 1.0f, 0.3f);
    }
    else if (currentState == INSTRUCTION) {
        renderText(w / 2 - 80, h / 2 + 100, GLUT_BITMAP_TIMES_ROMAN_24, "HOW TO PLAY", 1.0f, 1.0f, 0.0f);

        renderText(w / 2 - 170, h / 2 + 40, GLUT_BITMAP_HELVETICA_18, "1. Mouse Drag : Rotate the center cube", 1.0f, 1.0f, 1.0f);
        renderText(w / 2 - 170, h / 2 + 10, GLUT_BITMAP_HELVETICA_18, "2. 'A' / 'D' Keys : Rotate the octagon wall", 1.0f, 1.0f, 1.0f);
        renderText(w / 2 - 170, h / 2 - 20, GLUT_BITMAP_HELVETICA_18, "3. Break the BLUE CORE to clear!", 0.2f, 0.6f, 1.0f);

        renderText(w / 2 - 170, h / 2 - 50, GLUT_BITMAP_HELVETICA_18, "4. 'Z' Key : Dash to Center (10s CD)", 1.0f, 0.4f, 1.0f);

        renderText(w / 2 - 140, h / 2 - 100, GLUT_BITMAP_HELVETICA_18, "Press 'ESC' or 'ENTER' to Return", 0.6f, 0.6f, 0.6f);
    }
    else if (currentState == PLAYING) {
        int currentElapsed = accumulatedTime + (glutGet(GLUT_ELAPSED_TIME) - startTime);
        int minutes = (currentElapsed / 1000) / 60;
        int seconds = (currentElapsed / 1000) % 60;
        int milliseconds = currentElapsed % 1000;

        if (isEndlessMode) {
            sprintf(textBuffer, "Wave: %d", currentStage);
            renderText(20.0f, h - 30.0f, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 0.5f, 0.0f);
            renderText(w - 180.0f, 20.0f, GLUT_BITMAP_HELVETICA_18, "Press 'ESC' to Quit", 0.6f, 0.6f, 0.6f);
        }
        else {
            sprintf(textBuffer, "Stage: %d / 3", currentStage);
            renderText(20.0f, h - 30.0f, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 1.0f, 1.0f);
        }

        sprintf(textBuffer, "Time: %02d:%02d.%03d", minutes, seconds, milliseconds);
        renderText(20.0f, h - 55.0f, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 1.0f, 1.0f);

        int currentTick = glutGet(GLUT_ELAPSED_TIME);
        int remainCD = 10000 - (currentTick - lastSkillTime);

        if (remainCD <= 0) {
            renderText(20.0f, h - 80.0f, GLUT_BITMAP_HELVETICA_18, "Skill [Z] : READY!", 0.2f, 1.0f, 0.2f);
        }
        else {
            sprintf(textBuffer, "Skill [Z] : %.1f s", remainCD / 1000.0f);
            renderText(20.0f, h - 80.0f, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 0.3f, 0.3f);
        }
    }
    else if (currentState == STAGE_CLEAR) {
        sprintf(textBuffer, "STAGE %d CORE DUMPED!", currentStage);
        renderText(w / 2 - 110, h / 2 + 20, GLUT_BITMAP_TIMES_ROMAN_24, textBuffer, 0.0f, 1.0f, 0.0f);
        renderText(w / 2 - 110, h / 2 - 20, GLUT_BITMAP_HELVETICA_18, "Press 'ENTER' for Next Stage", 1.0f, 1.0f, 1.0f);
    }
    else if (currentState == ALL_CLEAR) {
        renderText(w / 2 - 120, h / 2 + 70, GLUT_BITMAP_TIMES_ROMAN_24, "CONGRATULATIONS!", 1.0f, 0.5f, 0.0f);
        renderText(w / 2 - 125, h / 2 + 30, GLUT_BITMAP_HELVETICA_18, "All Cores Dumped Successfully", 0.0f, 1.0f, 1.0f);
        renderText(w / 2 - 80, h / 2, GLUT_BITMAP_HELVETICA_18, "(Memory Leaks: 0)", 0.5f, 1.0f, 0.5f);

        sprintf(textBuffer, "Final Time: %.3f sec", finalClearTime / 1000.0f);
        renderText(w / 2 - 90, h / 2 - 40, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 1.0f, 0.0f);

        renderText(w / 2 - 110, h / 2 - 80, GLUT_BITMAP_HELVETICA_18, "Press 'ENTER' to Return Menu", 0.8f, 0.8f, 0.8f);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

// --- 입력 처리 (키보드, 마우스) ---
void keyboard(unsigned char key, int x, int y) {
    if (currentState == MENU) {
        if (key == '1') {
            isEndlessMode = false;
            currentStage = 1;
            accumulatedTime = 0;
            initStage(currentStage);
            currentState = PLAYING;
            startTime = glutGet(GLUT_ELAPSED_TIME);
        }
        else if (key == '2') {
            isEndlessMode = true;
            currentStage = 1;
            accumulatedTime = 0;
            initStage(currentStage);
            currentState = PLAYING;
            startTime = glutGet(GLUT_ELAPSED_TIME);
        }
        else if (key == '3') {
            currentState = INSTRUCTION;
        }
    }
    else if (currentState == INSTRUCTION) {
        if (key == 13 || key == 27) {
            currentState = MENU;
        }
    }
    else if (key == 13) {
        if (currentState == STAGE_CLEAR) {
            currentStage++;
            initStage(currentStage);
            currentState = PLAYING;
            startTime = glutGet(GLUT_ELAPSED_TIME);
        }
        else if (currentState == ALL_CLEAR) {
            loadHighScore();
            currentState = MENU;
        }
    }
    else if (currentState == PLAYING) {
        if (key == 27 && isEndlessMode) {
            loadHighScore();
            currentState = MENU;
        }
        if (key == 'a' || key == 'A') wallRotZ += 5.0f;
        if (key == 'd' || key == 'D') wallRotZ -= 5.0f;

        // --- 필살기 Z 버튼 트리거 로직 ---
        if (key == 'z' || key == 'Z') {
            int currentTick = glutGet(GLUT_ELAPSED_TIME);
            if (currentTick - lastSkillTime >= 10000) {
                float targetX = 0.0f;
                float targetY = 0.0f;

                // 1회차(0)는 정확히 중앙으로, 2회차(1 이상)부터는 다른 랜덤 좌표로
                if (skillUseCount > 0) {
                    // -2.5 ~ 2.5 사이의 랜덤 좌표 지정 (코어를 빗나가게 됨)
                    targetX = (rand() % 500 - 250) / 100.0f;
                    targetY = (rand() % 500 - 250) / 100.0f;

                    // 만약 공의 현재 위치와 너무 비슷해서 움직임이 없으면 보정
                    if (fabs(targetX - ballX) < 0.5f && fabs(targetY - ballY) < 0.5f) {
                        targetX += 1.5f;
                    }
                }

                // 목표 지점을 향하는 방향 벡터 계산
                float dx = targetX - ballX;
                float dy = targetY - ballY;
                float dist = sqrt(dx * dx + dy * dy);

                if (dist > 0.01f) {
                    float dashSpeed = 0.35f;
                    ballSpeedX = (dx / dist) * dashSpeed;
                    ballSpeedY = (dy / dist) * dashSpeed;
                }
                lastSkillTime = currentTick;
                skillUseCount++; // 스킬 사용 횟수 증가
            }
        }
    }
}

void mouse(int button, int state, int x, int y) {
    if (currentState == PLAYING && button == GLUT_LEFT_BUTTON) {
        if (state == GLUT_DOWN) {
            isDragging = true;
            lastMouseX = x;
            lastMouseY = y;
        }
        else if (state == GLUT_UP) {
            isDragging = false;
        }
    }
}

void motion(int x, int y) {
    if (isDragging && currentState == PLAYING) {
        int dx = x - lastMouseX;
        int dy = y - lastMouseY;
        blockRotX += dy * 0.5f;
        blockRotY += dx * 0.5f;
        lastMouseX = x;
        lastMouseY = y;
    }
}

// --- 화면 출력 및 타이머 ---
void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    gluLookAt(0.0, 5.0, 18.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

    drawAndMoveStars();

    if (currentState == PLAYING || currentState == STAGE_CLEAR) {
        drawOctagonWall();
        draw3x3x3Bricks();
        drawAndMoveBall();
    }

    drawUI();

    glutSwapBuffers();
}

void timer(int value) {
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)w / (double)h, 1.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(800, 800);
    glutCreateWindow("Octagon Reverse Gravity - Core Breaker");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutTimerFunc(0, timer, 0);

    glutMainLoop();
    return 0;
}
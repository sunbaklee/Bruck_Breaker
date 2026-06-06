#include <windows.h>
#include <delayimp.h>
#include <GL/glut.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#pragma warning(disable:4996)

// ============================================================
// DLL 내장 로더 - freeglut.dll을 exe 리소스에서 추출하여 로드
// ============================================================

#define IDR_FREEGLUT 256
static char g_freeglutDllPath[MAX_PATH];

/*
 * ExtractFreeglut
 * - exe 내부 리소스(RCDATA)에 내장된 freeglut.dll을 임시 폴더(%TEMP%)로 추출한다.
 * - glutInit() 호출 전에 반드시 먼저 실행되어야 한다.
 */
static void ExtractFreeglut(void) {
    HMODULE hSelf = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hSelf, MAKEINTRESOURCEW(IDR_FREEGLUT), RT_RCDATA);
    if (!hRes) return;
    HGLOBAL hData = LoadResource(hSelf, hRes);
    if (!hData) return;
    void* pData = LockResource(hData);
    DWORD dwSize = SizeofResource(hSelf, hRes);
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    snprintf(g_freeglutDllPath, MAX_PATH, "%sfreeglut.dll", tempDir);
    HANDLE hFile = CreateFileA(g_freeglutDllPath, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, pData, dwSize, &written, NULL);
        CloseHandle(hFile);
    }
}

/*
 * DliHook (Delay-Load 알림 훅)
 * - Windows DLL 지연 로드 시스템이 freeglut.dll을 찾으려 할 때 가로채어,
 *   ExtractFreeglut()이 추출한 임시 경로에서 직접 로드하도록 유도한다.
 * - 이를 통해 exe 옆에 DLL 파일 없이도 실행이 가능하다.
 */
static FARPROC WINAPI DliHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary &&
        _stricmp(pdli->szDll, "freeglut.dll") == 0 &&
        g_freeglutDllPath[0] != '\0') {
        HMODULE h = LoadLibraryA(g_freeglutDllPath);
        if (h) return (FARPROC)h;
    }
    return NULL;
}
extern "C" const PfnDliHook __pfnDliNotifyHook2 = DliHook;

// ============================================================
// 상수 및 열거형
// ============================================================

#define PI 3.14159265f

// 게임 화면 상태를 나타내는 열거형
// MENU: 메인 메뉴 / INSTRUCTION: 조작법 안내 / PLAYING: 플레이 중
// STAGE_CLEAR: 스테이지 클리어 / ALL_CLEAR: 전체 클리어
typedef enum { MENU, INSTRUCTION, PLAYING, STAGE_CLEAR, ALL_CLEAR } GameState;
GameState currentState = MENU;

// ============================================================
// 전역 변수
// ============================================================

bool isEndlessMode = false; // 무한 모드 여부
int currentStage = 1;       // 현재 스테이지 번호 (스토리: 1~3, 무한: 계속 증가)
int maxStage = 3;           // 스토리 모드 최대 스테이지 수

// 시간 측정 및 기록용 변수
int startTime = 0;          // 현재 스테이지 시작 시각 (ms)
int accumulatedTime = 0;    // 이전 스테이지까지의 누적 경과 시간 (ms)
int finalClearTime = 0;     // 전체 클리어 시 총 소요 시간 (ms)

float bestTimeSeconds = 9999.0f; // 스토리 모드 최단 클리어 기록 (초)
int bestWave = 0;                // 무한 모드 최고 도달 웨이브

// 필살기(Z키) 쿨타임 및 사용 횟수 관련 변수
int lastSkillTime = -10000; // 마지막 스킬 사용 시각 (쿨타임 계산용)
int skillUseCount = 0;      // 누적 스킬 사용 횟수 (1회차: 코어 조준, 이후: 랜덤)

// 3D 블록 회전 각도 (마우스 드래그로 조작)
float blockRotX = 15.0f;
float blockRotY = -15.0f;

int lastMouseX, lastMouseY; // 이전 프레임의 마우스 좌표 (드래그 계산용)
bool isDragging = false;    // 마우스 드래그 상태 여부

float wallRotZ = 0.0f;          // 팔각형 벽의 Z축 회전 각도 (A/D 키로 조작)
bool activeBricks[3][3][3];     // 3×3×3 벽돌 활성화 상태 배열

// 공의 위치 및 물리 속성
float ballX = 0.0f, ballY = -4.0f;     // 공의 현재 2D 위치
float ballSpeedX = 0.05f, ballSpeedY = 0.12f; // 공의 X/Y 방향 속도
float gravity = 0.002f;                // 매 프레임 Y 속도에 더해지는 중력 가속도
float maxSpeed = 0.25f;               // 공의 최대 허용 속도

// 우주 배경 별 파티클 (200개, 각 [x, y, z] 좌표 저장)
#define NUM_STARS 200
float stars[NUM_STARS][3];

// ============================================================
// 파일 입출력 - 최고 기록 저장/불러오기
// ============================================================

/*
 * loadHighScore
 * - "score.txt" 파일에서 스토리 최단 기록(bestTimeSeconds)과
 *   무한 모드 최고 웨이브(bestWave)를 읽어 전역 변수에 저장한다.
 * - 파일이 없거나 읽기 실패 시 기본값을 유지한다.
 */
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

/*
 * saveHighScore
 * - 현재 bestTimeSeconds와 bestWave를 "score.txt"에 기록한다.
 * - 스토리 클리어 최단 기록 또는 무한 모드 최고 웨이브 갱신 시 호출된다.
 */
void saveHighScore() {
    FILE* fp = fopen("score.txt", "w");
    if (fp != NULL) {
        fprintf(fp, "%f %d", bestTimeSeconds, bestWave);
        fclose(fp);
    }
}

// ============================================================
// 게임 로직 - 초기화
// ============================================================

/*
 * initStage
 * - 주어진 스테이지 번호(stage)에 맞게 게임 상태를 초기화한다.
 * - 공의 위치/속도, 블록 회전값, 스킬 쿨타임을 리셋한다.
 * - 모드에 따라 activeBricks 배열을 다르게 설정한다:
 *     스토리 1스테이지: 십자(+) 형태
 *     스토리 2스테이지: 꼭짓점 제외 형태
 *     스토리 3스테이지: 전체 27개 활성화
 *     무한 모드: 75% 확률 랜덤 (코어[1][1][1]는 항상 활성)
 * - 모든 모드에서 중앙 코어 [1][1][1]은 항상 활성 상태로 강제 설정된다.
 */
void initStage(int stage) {
    ballX = 0.0f; ballY = -4.0f;
    ballSpeedX = 0.05f; ballSpeedY = 0.12f;
    blockRotX = 15.0f; blockRotY = -15.0f;

    lastSkillTime = -10000;
    skillUseCount = 0;

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
    activeBricks[1][1][1] = true; // 중앙 코어는 항상 마지막 목표
}

/*
 * init
 * - OpenGL 렌더링 환경과 게임 초기 상태를 설정한다.
 * - 배경색(어두운 남색), 깊이 테스트, 조명(GL_LIGHT0)을 활성화한다.
 * - 별 파티클 200개의 초기 위치를 랜덤으로 배치한다.
 * - loadHighScore()를 호출해 저장된 기록을 불러온다.
 */
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

// ============================================================
// 렌더링 - 배경 및 게임 오브젝트
// ============================================================

/*
 * drawAndMoveStars
 * - 우주 배경을 표현하는 별 파티클 200개를 렌더링하고 이동시킨다.
 * - 각 별은 Z축 방향으로 앞으로 이동하며 카메라를 통과하면 뒤쪽으로 재배치된다.
 * - 거리(Z값)에 따라 밝기를 조절해 원근감을 연출한다.
 * - 조명을 임시 비활성화하여 별이 조명 영향을 받지 않게 한다.
 */
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

/*
 * drawOctagonWall
 * - 공이 반사되는 팔각형 경계벽을 렌더링한다.
 * - 시간 함수(sin/cos)를 사용해 색상이 주기적으로 변화하는 무지개 효과를 적용한다.
 * - 두께가 다른 선 3겹(glLineWidth)으로 글로우(glow) 효과를 표현한다.
 * - wallRotZ 값에 따라 A/D 키로 벽 전체를 Z축 기준으로 회전시킬 수 있다.
 */
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

/*
 * checkCollision
 * - 공(ballX, ballY)이 활성화된 3D 벽돌과 충돌하는지 검사한다.
 * - 현재 블록의 회전 각도(blockRotX, blockRotY)를 적용하여
 *   각 벽돌의 3D 위치를 2D 평면에 투영한 후 공과의 거리를 계산한다.
 * - 거리가 0.9f 미만이면 충돌로 판정, 해당 벽돌을 비활성화하고
 *   공의 속도를 반전시킨다. (고속이면 감속, 저속이면 가속)
 * - 충돌 발생 시 true, 없으면 false를 반환한다.
 */
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

                // Y축 회전 적용
                float rx1 = lx * cos(radY) + lz * sin(radY);
                float ry1 = ly;
                float rz1 = -lx * sin(radY) + lz * cos(radY);

                // X축 회전 적용
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
                        ballSpeedX *= -0.5f;  // 고속 충돌: 감속 반사
                        ballSpeedY *= -0.5f;
                    }
                    else {
                        ballSpeedX *= -1.02f; // 저속 충돌: 가속 반사
                        ballSpeedY *= -1.02f;
                    }
                    return true;
                }
            }
        }
    }
    return false;
}

/*
 * checkStageClear
 * - 중앙 코어 벽돌([1][1][1])이 파괴되었는지 확인하여 스테이지 클리어를 처리한다.
 * - 스토리 모드: 마지막 스테이지이면 ALL_CLEAR, 아니면 STAGE_CLEAR 상태로 전환하고
 *   최단 기록을 갱신하면 saveHighScore()를 호출한다.
 * - 무한 모드: 스테이지 번호를 증가시키고 즉시 다음 스테이지를 초기화한다.
 *   최고 웨이브 갱신 시 saveHighScore()를 호출한다.
 */
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

/*
 * draw3x3x3Bricks
 * - 3×3×3 = 최대 27개의 벽돌을 3D 공간에 렌더링한다.
 * - blockRotX, blockRotY 각도로 전체 블록 덩어리를 회전시킨다.
 * - activeBricks가 false인 위치는 건너뛴다.
 * - 중앙 코어(0,0,0)는 파란색, 나머지는 회색으로 구분하며
 *   모든 벽돌에 와이어프레임(glutWireCube)을 덧그려 테두리를 강조한다.
 */
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

                if (x == 0 && y == 0 && z == 0) glColor3f(0.2f, 0.4f, 1.0f); // 코어: 파란색
                else glColor3f(0.3f, 0.3f, 0.4f);                             // 일반: 회색

                glutSolidCube(1.0f);
                glColor3f(0.5f, 0.8f, 1.0f);
                glutWireCube(1.01f); // 테두리
                glPopMatrix();
            }
        }
    }
    glPopMatrix();
}

/*
 * draw2DBall
 * - 지정된 반지름(radius)의 원형 공을 2D 방식으로 렌더링한다.
 * - GL_TRIANGLE_FAN으로 중심점에서 30개 꼭짓점을 연결해 원을 근사한다.
 * - 공 색상은 주황색(1.0, 0.5, 0.0)으로 고정된다.
 */
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

/*
 * drawAndMoveBall
 * - 매 프레임 공의 물리 시뮬레이션을 수행하고 렌더링한다.
 * - PLAYING 상태가 아니면 즉시 반환한다.
 * - 처리 순서:
 *     1) 중력 적용: 매 프레임 ballSpeedY에 gravity를 더한다.
 *     2) 위치 갱신: ballX/ballY에 각 방향 속도를 더한다.
 *     3) 벽 충돌 반사: 원형 경계(반지름 7.0)를 벗어나면 법선 벡터로 반사하고
 *        벽 충돌 후 속도를 1.02배 가속한다.
 *     4) 벽돌 충돌 검사: checkCollision() 호출.
 *     5) 클리어 검사: checkStageClear() 호출.
 *     6) 최대 속도 제한: 초과 시 0.95 감속.
 *     7) 공 렌더링: 반지름 0.4의 원을 현재 위치에 그린다.
 */
void drawAndMoveBall() {
    if (currentState != PLAYING) return;

    ballY += ballSpeedY;
    ballX += ballSpeedX;
    ballSpeedY += gravity;

    float distFromCenter = sqrt(ballX * ballX + ballY * ballY);
    float wallRadius = 7.0f;

    if (distFromCenter > wallRadius) {
        // 법선 벡터 계산 후 속도 반사
        float nx = ballX / distFromCenter;
        float ny = ballY / distFromCenter;
        float dot = ballSpeedX * nx + ballSpeedY * ny;
        ballSpeedX = ballSpeedX - 2 * dot * nx;
        ballSpeedY = ballSpeedY - 2 * dot * ny;

        ballSpeedX *= 1.02f; // 벽 반사 시 가속
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

// ============================================================
// 렌더링 - UI 및 텍스트
// ============================================================

/*
 * renderText
 * - 지정된 2D 좌표(x, y)에 문자열을 렌더링하는 헬퍼 함수이다.
 * - font: GLUT 비트맵 폰트 종류 (예: GLUT_BITMAP_HELVETICA_18)
 * - r, g, b: 텍스트 색상 (0.0 ~ 1.0)
 * - glRasterPos2f로 위치를 설정하고 각 문자를 glutBitmapCharacter로 출력한다.
 */
void renderText(float x, float y, void* font, const char* string, float r, float g, float b) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (const char* c = string; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

/*
 * drawUI
 * - 현재 게임 상태(currentState)에 따라 2D HUD와 화면 전환 텍스트를 렌더링한다.
 * - 3D 투영을 잠시 끄고 gluOrtho2D로 2D 좌표계로 전환한 후 텍스트를 그린다.
 *
 * 상태별 출력 내용:
 *   MENU       : 타이틀, 최고 기록, 모드 선택 안내
 *   INSTRUCTION: 조작법 설명 (마우스 드래그, A/D 키, Z 스킬)
 *   PLAYING    : 현재 스테이지/웨이브, 경과 시간, 스킬 쿨타임
 *   STAGE_CLEAR: 스테이지 클리어 메시지 및 다음 진행 안내
 *   ALL_CLEAR  : 축하 메시지, 최종 클리어 시간
 */
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
        renderText(w / 2 - 110, h / 2 + 80, GLUT_BITMAP_TIMES_ROMAN_24, "3D BRICK BREAKER", 0.0f, 1.0f, 1.0f);

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
        renderText(w / 2 - 200, h / 2 - 50, GLUT_BITMAP_HELVETICA_18, "4. 'Z' Key : Dash to Target (1st: Core, Next: Random) (10s CD)", 1.0f, 0.4f, 1.0f);

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
        sprintf(textBuffer, "STAGE %d CLEAR!", currentStage);
        renderText(w / 2 - 80, h / 2 + 20, GLUT_BITMAP_TIMES_ROMAN_24, textBuffer, 0.0f, 1.0f, 0.0f);
        renderText(w / 2 - 110, h / 2 - 20, GLUT_BITMAP_HELVETICA_18, "Press 'ENTER' for Next Stage", 1.0f, 1.0f, 1.0f);
    }
    else if (currentState == ALL_CLEAR) {
        renderText(w / 2 - 120, h / 2 + 60, GLUT_BITMAP_TIMES_ROMAN_24, "CONGRATULATIONS!", 1.0f, 0.5f, 0.0f);
        renderText(w / 2 - 70, h / 2 + 20, GLUT_BITMAP_HELVETICA_18, "All Stages Cleared!", 0.0f, 1.0f, 1.0f);

        sprintf(textBuffer, "Final Time: %.3f sec", finalClearTime / 1000.0f);
        renderText(w / 2 - 90, h / 2 - 20, GLUT_BITMAP_HELVETICA_18, textBuffer, 1.0f, 1.0f, 0.0f);

        renderText(w / 2 - 110, h / 2 - 60, GLUT_BITMAP_HELVETICA_18, "Press 'ENTER' to Return Menu", 0.8f, 0.8f, 0.8f);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

// ============================================================
// 입력 처리
// ============================================================

/*
 * keyboard
 * - GLUT 키보드 콜백 함수로, 키 입력에 따라 게임 상태를 전환하거나 오브젝트를 조작한다.
 *
 * 처리 내용:
 *   MENU 상태:
 *     '1' → 스토리 모드 시작 (1스테이지, 타이머 초기화)
 *     '2' → 무한 모드 시작
 *     '3' → 조작법 화면(INSTRUCTION)으로 전환
 *   INSTRUCTION 상태:
 *     ESC(27) 또는 ENTER(13) → 메인 메뉴로 복귀
 *   STAGE_CLEAR 상태:
 *     ENTER → 다음 스테이지 초기화 후 PLAYING으로 전환
 *   ALL_CLEAR 상태:
 *     ENTER → 최고 기록 갱신 후 메인 메뉴로 복귀
 *   PLAYING 상태:
 *     ESC  → 무한 모드 중 메뉴로 복귀
 *     A/a  → 팔각형 벽을 반시계 방향으로 5도 회전
 *     D/d  → 팔각형 벽을 시계 방향으로 5도 회전
 *     Z/z  → 필살기: 공을 목표 지점으로 대시 (10초 쿨타임)
 *             첫 번째 사용: 중앙 코어(0,0)로 조준
 *             이후 사용: 랜덤 좌표로 조준
 */
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

        if (key == 'z' || key == 'Z') {
            int currentTick = glutGet(GLUT_ELAPSED_TIME);
            if (currentTick - lastSkillTime >= 10000) { // 10초 쿨타임 확인
                float targetX = 0.0f;
                float targetY = 0.0f;

                if (skillUseCount > 0) {
                    // 첫 사용 이후: 랜덤 목표 지점 설정 (공과 너무 가까우면 보정)
                    targetX = (rand() % 500 - 250) / 100.0f;
                    targetY = (rand() % 500 - 250) / 100.0f;

                    if (fabs(targetX - ballX) < 0.5f && fabs(targetY - ballY) < 0.5f) {
                        targetX += 1.5f;
                    }
                }

                float dx = targetX - ballX;
                float dy = targetY - ballY;
                float dist = sqrt(dx * dx + dy * dy);

                if (dist > 0.01f) {
                    float dashSpeed = 0.35f;
                    ballSpeedX = (dx / dist) * dashSpeed;
                    ballSpeedY = (dy / dist) * dashSpeed;
                }
                lastSkillTime = currentTick;
                skillUseCount++;
            }
        }
    }
}

/*
 * mouse
 * - GLUT 마우스 버튼 콜백 함수이다.
 * - PLAYING 상태에서 좌클릭 누름: isDragging = true, 드래그 기준점 저장
 * - 좌클릭 뗌: isDragging = false
 */
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

/*
 * motion
 * - GLUT 마우스 이동 콜백 함수이다.
 * - isDragging이 true이고 PLAYING 상태일 때만 동작한다.
 * - 이전 프레임 대비 마우스 이동량(dx, dy)에 비례해
 *   blockRotX(상하), blockRotY(좌우)를 갱신하여 3D 블록을 회전시킨다.
 * - 감도는 0.5f (이동 픽셀당 0.5도 회전).
 */
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

// ============================================================
// GLUT 콜백 - 화면 출력, 타이머, 뷰포트
// ============================================================

/*
 * display
 * - GLUT 디스플레이 콜백 함수로, 매 프레임 전체 화면을 렌더링한다.
 * - 처리 순서:
 *     1) 버퍼 초기화 (색상 + 깊이)
 *     2) 카메라 설정: gluLookAt으로 시점 (0,5,18) → 원점
 *     3) 별 배경 렌더링 (drawAndMoveStars)
 *     4) PLAYING/STAGE_CLEAR 상태: 팔각형 벽, 3D 블록, 공 렌더링
 *     5) UI 렌더링 (drawUI)
 *     6) 더블 버퍼 스왑 (glutSwapBuffers)
 */
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

/*
 * timer
 * - GLUT 타이머 콜백 함수로, 약 60fps(16ms 간격)로 화면 갱신을 요청한다.
 * - glutPostRedisplay()로 display() 재호출을 예약하고,
 *   glutTimerFunc()로 자기 자신을 16ms 후에 다시 등록한다.
 */
void timer(int value) {
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

/*
 * reshape
 * - 윈도우 크기 변경 시 호출되는 GLUT 콜백 함수이다.
 * - 새로운 너비(w)와 높이(h)에 맞게 뷰포트를 재설정한다.
 * - gluPerspective로 투영 행렬을 갱신한다:
 *     시야각(FOV): 45도, 종횡비: w/h, 근평면: 1.0, 원평면: 100.0
 */
void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)w / (double)h, 1.0, 100.0);
    glMatrixMode(GL_MODELVIEW);
}

/*
 * main
 * - 프로그램 진입점.
 * - ExtractFreeglut()으로 내장 DLL을 추출한 후 GLUT 환경을 초기화한다.
 * - 800×800 윈도우 생성, 더블 버퍼 + RGB + 깊이 버퍼 모드 설정.
 * - 각 GLUT 콜백 함수를 등록하고 glutMainLoop()으로 이벤트 루프를 시작한다.
 */
int main(int argc, char** argv) {
    ExtractFreeglut();
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(800, 800);
    glutCreateWindow("3D Brick Breaker");

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

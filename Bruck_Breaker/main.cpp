#include <GL/glut.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>  // sprintf 사용을 위해 추가
#pragma warning(disable:4996)

#define PI 3.14159265f

// --- 전역 변수 ---
float blockRotX = 15.0f;
float blockRotY = -15.0f;

int lastMouseX, lastMouseY;
bool isDragging = false;

float wallRotZ = 0.0f;

bool activeBricks[3][3][3];

// 공 속도 및 물리엔진 튜닝 (반대편까지 잘 닿도록 수정)
float ballX = 0.0f, ballY = -4.0f;
float ballSpeedX = 0.05f, ballSpeedY = 0.12f;
float gravity = 0.002f; // 중력을 살짝 약하게 하여 더 멀리 날아가게 함
float maxSpeed = 0.25f; // 최대 속도 제한을 0.18에서 0.25로 늘려 반동 범위를 확보

// 타이머용 변수
int startTime = 0;

// --- 우주 배경 파티클용 변수 ---
#define NUM_STARS 200
float stars[NUM_STARS][3];

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

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                activeBricks[i][j][k] = true;
            }
        }
    }

    for (int i = 0; i < NUM_STARS; i++) {
        stars[i][0] = (rand() % 400 - 200) / 10.0f;
        stars[i][1] = (rand() % 400 - 200) / 10.0f;
        stars[i][2] = (rand() % 400 - 200) / 10.0f - 10.0f;
    }

    // 프로그램 시작 시간 기록
    startTime = glutGet(GLUT_ELAPSED_TIME);
}

// --- 우주 배경 별 그리기 및 이동 ---
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

// --- 8각형 네온사인 벽 그리기 ---
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

// --- 공과 벽돌의 충돌 검사 및 처리 ---
void checkCollision() {
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

                    // 파괴 시 튕기는 힘 유지
                    ballSpeedX *= -1.02f;
                    ballSpeedY *= -1.02f;
                    return;
                }
            }
        }
    }
}

// --- 3x3x3 입체 벽돌 그리기 ---
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

                if (x == 0 && y == 0 && z == 0) {
                    glColor3f(0.0f, 1.0f, 1.0f);
                }
                else {
                    glColor3f(0.3f, 0.3f, 0.4f);
                }

                glutSolidCube(1.0f);

                glColor3f(0.5f, 0.8f, 1.0f);
                glutWireCube(1.01f);
                glPopMatrix();
            }
        }
    }
    glPopMatrix();
}

// --- 2D 공 그리기 함수 ---
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

// --- 공 움직임 및 벽 충돌 처리 ---
void drawAndMoveBall() {
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

        // 벽 반사 시 운동 에너지를 살짝 보존하여 반대편 끝까지 닿게 함
        ballSpeedX *= 1.02f;
        ballSpeedY *= 1.02f;

        ballX = nx * wallRadius;
        ballY = ny * wallRadius;
    }

    checkCollision();

    float currentSpeed = sqrt(ballSpeedX * ballSpeedX + ballSpeedY * ballSpeedY);
    if (currentSpeed > maxSpeed) {
        ballSpeedX = (ballSpeedX / currentSpeed) * maxSpeed;
        ballSpeedY = (ballSpeedY / currentSpeed) * maxSpeed;
    }

    glPushMatrix();
    glTranslatef(ballX, ballY, 0.0f);
    draw2DBall(0.4f);
    glPopMatrix();
}

// --- UI (타이머) 그리기 함수 ---
void drawUI() {
    // 2D 직교 투영으로 전환하여 화면에 고정된 텍스트 출력
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

    // 경과 시간 계산 (밀리초)
    int elapsedTime = glutGet(GLUT_ELAPSED_TIME) - startTime;
    int minutes = (elapsedTime / 1000) / 60;
    int seconds = (elapsedTime / 1000) % 60;
    int milliseconds = elapsedTime % 1000;

    char timeStr[64];
    sprintf(timeStr, "Time: %02d:%02d.%03d", minutes, seconds, milliseconds);

    // 텍스트 위치 및 색상 설정
    glColor3f(1.0f, 1.0f, 1.0f); // 흰색
    glRasterPos2f(20.0f, h - 30.0f); // 좌측 상단

    // 문자열 한 글자씩 출력
    for (char* c = timeStr; *c != '\0'; c++) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

// --- 마우스 클릭 처리 ---
void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON) {
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

// --- 마우스 드래그 처리 ---
void motion(int x, int y) {
    if (isDragging) {
        int dx = x - lastMouseX;
        int dy = y - lastMouseY;

        blockRotX += dy * 0.5f;
        blockRotY += dx * 0.5f;

        lastMouseX = x;
        lastMouseY = y;
    }
}

// --- 키보드 입력 처리 ---
void keyboard(unsigned char key, int x, int y) {
    switch (key) {
    case 'a':
    case 'A':
        wallRotZ += 5.0f;
        break;
    case 'd':
    case 'D':
        wallRotZ -= 5.0f;
        break;
    }
}

// --- 화면 출력 함수 ---
void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();

    gluLookAt(0.0, 5.0, 18.0,
        0.0, 0.0, 0.0,
        0.0, 1.0, 0.0);

    drawAndMoveStars();
    drawOctagonWall();
    draw3x3x3Bricks();
    drawAndMoveBall();

    // UI (타이머) 그리기 - 가장 마지막에 그려서 최상단에 띄움
    drawUI();

    glutSwapBuffers();
}

// --- 애니메이션 타이머 ---
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
    glutCreateWindow("Octagon Reverse Gravity - Timer Edition");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);

    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    glutKeyboardFunc(keyboard);

    glutTimerFunc(0, timer, 0);

    glutMainLoop();
    return 0;
}
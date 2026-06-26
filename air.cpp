/*
 * 太空射击游戏 - V1.5 (修复菜单按键穿透)
 * 编译：VS2022 + EasyX x64
 */

#include <graphics.h>
#include <vector>
#include <ctime>
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <stdio.h>

using namespace std;

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const int MAX_ENEMIES = 40;
const int MAX_BULLETS = 200;
const int MAX_PARTICLES = 800;
const int MAX_LASERS = 10;

bool g_keys[256] = { false };

#define GAME_VERSION _T("V1.5")

// ---------- 皮肤 ----------
enum SkinType { SKIN_DEFAULT = 0, SKIN_FLAME = 1, SKIN_ICE = 2, SKIN_THUNDER = 3, SKIN_GOLD = 4, SKIN_STEALTH = 5, SKIN_COUNT = 6 };
struct SkinConfig { COLORREF bodyColor, cockpitColor, wingColor, flameColor; TCHAR name[20]; };
SkinConfig g_skins[SKIN_COUNT] = {
    {RGB(0,150,255),RGB(0,255,255),RGB(0,100,200),RGB(255,150,0),_T("默认")},
    {RGB(255,80,0),RGB(255,200,50),RGB(200,50,0),RGB(255,255,0),_T("火焰")},
    {RGB(100,200,255),RGB(200,240,255),RGB(50,150,220),RGB(150,220,255),_T("冰霜")},
    {RGB(200,200,50),RGB(255,255,150),RGB(150,150,30),RGB(255,255,200),_T("雷电")},
    {RGB(255,200,50),RGB(255,240,150),RGB(200,150,30),RGB(255,215,0),_T("黄金")},
    {RGB(60,60,80),RGB(100,100,150),RGB(30,30,50),RGB(80,80,120),_T("隐形")}
};

// ---------- 难度 ----------
enum DifficultyLevel { DIFF_EASY = 0, DIFF_NORMAL = 1, DIFF_HARD = 2, DIFF_COUNT = 3 };
const TCHAR* g_diffNames[DIFF_COUNT] = { _T("简单"), _T("普通"), _T("困难") };
const double g_diffSpawnRate[DIFF_COUNT] = { 30.0, 20.0, 12.0 };
const int g_diffStartEnemies[DIFF_COUNT] = { 5, 8, 12 };
const double g_diffEnemySpeed[DIFF_COUNT] = { 0.7, 1.0, 1.4 };

// ---------- 地图 ----------
enum MapType { MAP_STAR = 0, MAP_DESERT = 1, MAP_CYBER = 2, MAP_COUNT = 3 };
const TCHAR* g_mapNames[MAP_COUNT] = { _T("星空"), _T("沙漠"), _T("赛博") };

enum GameMode { MODE_SINGLE, MODE_COOP, MODE_SURVIVAL };
enum GameState {
    STATE_MENU, STATE_MODE_SELECT,
    STATE_SURVIVAL_MODE_SELECT,
    STATE_DIFF_SELECT, STATE_MAP_SELECT, STATE_SKIN_SELECT,
    STATE_PLAYING, STATE_PAUSED,
    STATE_GAMEOVER,
    STATE_SURVIVAL_SUCCESS, STATE_SURVIVAL_FAIL,
    STATE_HELP, STATE_SETTINGS
};

LRESULT CALLBACK GameWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN: g_keys[wParam] = true; return 0;
    case WM_KEYUP: g_keys[wParam] = false; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

struct Particle { double x, y, vx, vy, life; COLORREF color; int size; bool active; };

class Player;

struct Laser {
    double startX, startY, endX, endY;
    int preTimer, fireTimer, phase;
    bool active;
    int damage;
    Laser() : startX(0), startY(0), endX(0), endY(0), preTimer(0), fireTimer(0), phase(0), active(false), damage(1) {}
    void activate(double sx, double sy, double ex, double ey) {
        double dx = ex - sx, dy = ey - sy, len = sqrt(dx * dx + dy * dy);
        if (len > 0) { dx /= len; dy /= len; ex += dx * 300; ey += dy * 300; }
        startX = sx; startY = sy; endX = ex; endY = ey;
        preTimer = 30; fireTimer = 20; phase = 0; active = true;
    }
    void update() { if (!active) return; if (phase == 0) { preTimer--; if (preTimer <= 0) phase = 1; } else { fireTimer--; if (fireTimer <= 0) active = false; } }
    void draw() {
        if (!active) return;
        setlinecolor(phase == 0 ? RGB(255, 150, 150) : RGB(255, 50, 50));
        setlinestyle(PS_SOLID, phase == 0 ? 2 : 6);
        line((int)startX, (int)startY, (int)endX, (int)endY);
        setlinestyle(PS_SOLID, 2); setlinecolor(RGB(255, 255, 255));
    }
    bool checkCollision(const Player& player);
};

class Bullet {
public:
    double x, y, vx, vy; int playerID; bool active; int damage; COLORREF color; int radius;
    Bullet(double sx, double sy, int pid, double spd = 8.0, double angle = 0.0, COLORREF clr = RGB(0, 255, 255))
        :x(sx), y(sy), vx(0), vy(0), playerID(pid), active(true), damage(1), radius(3), color(clr) {
        if (pid > 0) { vy = -spd; vx = angle * spd * 0.3; }
        else { vy = spd; vx = 0; color = RGB(255, 80, 0); radius = 4; }
    }
    void update() { x += vx; y += vy; if (y<-10 || y>SCREEN_HEIGHT + 10 || x<-10 || x>SCREEN_WIDTH + 10) active = false; }
    void draw() {
        if (!active)return;
        setfillcolor(color); setlinecolor(RGB(255, 255, 200));
        if (playerID > 0) solidrectangle((int)x - 2, (int)y - 6, (int)x + 2, (int)y + 6);
        else { setlinestyle(PS_SOLID, 2); solidcircle((int)x, (int)y, radius); setlinestyle(PS_SOLID, 0); }
    }
};

class Enemy {
public:
    double x, y, speedX, speedY;
    int health, maxHealth, type, shootTimer, shootCooldown, movePattern;
    double moveTimer;
    bool active;
    COLORREF color;
    bool fireLaser;
    Enemy(double sx, double sy, int t = 0) :x(sx), y(sy), active(true), type(t), moveTimer(0), shootTimer(0), fireLaser(false) {
        switch (t) {
        case 0: speedX = 1.5; speedY = 1.0; health = 1; maxHealth = 1; color = RGB(255, 100, 100); shootCooldown = 80; movePattern = 0; break;
        case 1: speedX = 3.0; speedY = 2.0; health = 1; maxHealth = 1; color = RGB(255, 200, 50); shootCooldown = 60; movePattern = 1; break;
        case 2: speedX = 0.8; speedY = 0.5; health = 3; maxHealth = 3; color = RGB(200, 50, 200); shootCooldown = 45; movePattern = 0; break;
        case 3: speedX = 1.0; speedY = 0.8; health = 10; maxHealth = 10; color = RGB(255, 200, 0); shootCooldown = 40; movePattern = 1; break;
        }
        shootTimer = rand() % shootCooldown;
    }
    void update() {
        if (!active) return;
        moveTimer += 0.1;
        switch (movePattern) {
        case 0: y += speedY; x += speedX; if (x <= 50 || x >= SCREEN_WIDTH - 50) speedX = -speedX; break;
        case 1: y += speedY; x += sin(moveTimer * 2) * (type == 3 ? 5.0 : 3.0); if (x < 50)x = 50; if (x > SCREEN_WIDTH - 50)x = SCREEN_WIDTH - 50; break;
        case 2: x += speedX; y += speedY; break;
        }
        shootTimer++;
        if (y > SCREEN_HEIGHT + 60 || y < -60 || x<-60 || x>SCREEN_WIDTH + 60) active = false;
    }
    bool canShoot() {
        if (shootTimer >= shootCooldown && y > 50 && y < SCREEN_HEIGHT - 100) {
            shootTimer = 0;
            fireLaser = (type == 3 && rand() % 100 < 30);
            return true;
        }
        return false;
    }
    void draw() {
        if (!active) return;
        setfillcolor(color); setlinecolor(RGB(255, 255, 255));
        switch (type) {
        case 0: { POINT pts[3] = { {int(x),int(y - 15)},{int(x - 12),int(y + 10)},{int(x + 12),int(y + 10)} }; solidpolygon(pts, 3); break; }
        case 1: { POINT pts[4] = { {int(x),int(y - 12)},{int(x + 10),int(y)},{int(x),int(y + 12)},{int(x - 10),int(y)} }; solidpolygon(pts, 4); break; }
        case 2: solidcircle(int(x), int(y), 18); setfillcolor(RGB(150, 30, 150)); solidcircle(int(x), int(y), 12); break;
        case 3: solidcircle(int(x), int(y), 25); setfillcolor(RGB(200, 150, 0)); solidcircle(int(x), int(y), 18); setfillcolor(RGB(255, 200, 0)); solidcircle(int(x), int(y), 10); break;
        }
        if (maxHealth > 2) {
            int bw = type == 3 ? 60 : 40, bx = int(x) - bw / 2, by = int(y) - 35;
            setfillcolor(RGB(100, 100, 100)); solidrectangle(bx, by, bx + bw, by + 5);
            setfillcolor(RGB(0, 255, 0)); solidrectangle(bx, by, bx + int(bw * health / maxHealth), by + 5);
        }
    }
};

class Player {
public:
    double x, y;
    int lives, score, combo, invincibleTimer, shootCooldown, currentShootTimer, weaponLevel, playerID, skinType;
    double comboTime;
    bool active;
    SkinConfig skin;
    Player(int id) :x(id == 1 ? SCREEN_WIDTH / 3 : SCREEN_WIDTH * 2 / 3), y(SCREEN_HEIGHT - 80),
        lives(3), score(0), combo(0), comboTime(0), active(true), invincibleTimer(0),
        shootCooldown(15), currentShootTimer(0), weaponLevel(1), playerID(id), skinType(SKIN_DEFAULT) {
        skin = g_skins[SKIN_DEFAULT];
    }
    void setSkin(int t) { skinType = t; skin = g_skins[t]; }
    COLORREF getBulletColor() const { return skin.bodyColor; }
    void moveLeft() { if (x > 50) x -= 5; }
    void moveRight() { if (x < SCREEN_WIDTH - 50) x += 5; }
    void moveUp() { if (y > 30) y -= 5; }
    void moveDown() { if (y < SCREEN_HEIGHT - 30) y += 5; }
    void update() {
        if (invincibleTimer > 0) invincibleTimer--;
        if (comboTime > 0) { comboTime--; if (comboTime <= 0) combo = 0; }
        if (currentShootTimer > 0) currentShootTimer--;
    }
    bool canShoot() { if (currentShootTimer <= 0) { currentShootTimer = shootCooldown; return true; } return false; }
    void draw() {
        if (!active) return;
        if (invincibleTimer > 0 && invincibleTimer % 4 < 2) return;
        setfillcolor(skin.flameColor);
        int fh = 25 + rand() % 5;
        if (skinType == SKIN_FLAME) fh = 30 + rand() % 8;
        if (skinType == SKIN_ICE) fh = 20 + rand() % 3;
        POINT flame[3] = { {int(x - 8),int(y + 15)},{int(x),int(y + fh)},{int(x + 8),int(y + 15)} }; solidpolygon(flame, 3);
        if (skinType == SKIN_THUNDER && rand() % 3 == 0) {
            setfillcolor(RGB(255, 255, 200));
            POINT lt[4] = { {int(x),int(y + 15)},{int(x - 5),int(y + 20)},{int(x + 5),int(y + 25)},{int(x),int(y + 28)} }; solidpolygon(lt, 4);
        }
        setfillcolor(skin.wingColor);
        POINT lw[3] = { {int(x - 15),int(y + 5)},{int(x - 25),int(y + 15)},{int(x - 10),int(y + 15)} };
        POINT rw[3] = { {int(x + 15),int(y + 5)},{int(x + 25),int(y + 15)},{int(x + 10),int(y + 15)} };
        solidpolygon(lw, 3); solidpolygon(rw, 3);
        if (skinType == SKIN_GOLD) { setfillcolor(RGB(255, 215, 0)); solidcircle(int(x), int(y), 22); }
        setfillcolor(skin.bodyColor);
        POINT body[3] = { {int(x),int(y - 20)},{int(x - 15),int(y + 15)},{int(x + 15),int(y + 15)} }; solidpolygon(body, 3);
        setfillcolor(skin.cockpitColor);
        POINT cockpit[3] = { {int(x),int(y - 10)},{int(x - 7),int(y + 5)},{int(x + 7),int(y + 5)} }; solidpolygon(cockpit, 3);
        if (skinType == SKIN_ICE && rand() % 5 == 0) {
            setfillcolor(RGB(200, 240, 255));
            for (int i = 0; i < 3; i++) solidcircle(int(x) - 10 + rand() % 20, int(y) - 10 + rand() % 25, 1 + rand() % 2);
        }
    }
};

bool Laser::checkCollision(const Player& player) {
    if (!active || phase != 1) return false;
    if (!player.active) return false;
    double px = player.x, py = player.y;
    double dx = endX - startX, dy = endY - startY, lenSq = dx * dx + dy * dy;
    if (lenSq == 0) return false;
    double t = ((px - startX) * dx + (py - startY) * dy) / lenSq;
    t = max(0.0, min(1.0, t));
    double cx = startX + t * dx, cy = startY + t * dy;
    double dist = sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
    return dist < 20.0;
}

class PowerUp {
public:
    double x, y, lifeTime; int type; bool active;
    PowerUp(double sx, double sy, int t) :x(sx), y(sy), type(t), active(true), lifeTime(300) {}
    void update() { y += 1.5; lifeTime--; if (y > SCREEN_HEIGHT + 20 || lifeTime <= 0) active = false; }
    void draw() {
        if (!active || int(lifeTime) % 10 < 5) return;
        switch (type) {
        case 0: setfillcolor(RGB(255, 50, 50)); solidrectangle(int(x) - 4, int(y) - 12, int(x) + 4, int(y) + 12); solidrectangle(int(x) - 12, int(y) - 4, int(x) + 12, int(y) + 4); break;
        case 1: setfillcolor(RGB(255, 215, 0)); solidcircle(int(x), int(y), 10); break;
        case 2: { POINT pts[4] = { {int(x),int(y - 12)},{int(x + 10),int(y)},{int(x),int(y + 12)},{int(x - 10),int(y)} }; setfillcolor(RGB(100, 200, 255)); solidpolygon(pts, 4); break; }
        }
    }
};

class SpaceGame {
    Player player1, player2;
    vector<Bullet> bullets;
    vector<Enemy> enemies;
    vector<Particle> particles;
    vector<PowerUp> powerUps;
    vector<Laser> lasers;
    GameState state, prevState;          // prevState 用于检测状态变化
    GameMode mode;
    int enemySpawnTimer, bossSpawnTimer, difficulty, maxEnemiesOnScreen, totalScore;
    IMAGE* bg;
    bool enterPressed, escPressed, pPressed, key1Pressed, key2Pressed, key3Pressed, spacePressed;
    int highScore, skinSelectIndex, selectingPlayer, p1Skin, p2Skin;
    DifficultyLevel diffLevel;
    MapType mapType;
    int diffSelectIndex, mapSelectIndex;
    double baseSpawnRate;

    int survivalTimer;
    const int SURVIVAL_DURATION = 60 * 60 * 5;
    int survivalPlayerCount;

    bool showParticles;
    int menuSel;

public:
    SpaceGame() :player1(1), player2(2), state(STATE_MENU), prevState(STATE_MENU), mode(MODE_SINGLE),
        enemySpawnTimer(0), bossSpawnTimer(0), difficulty(1), maxEnemiesOnScreen(8), totalScore(0), bg(nullptr),
        enterPressed(false), escPressed(false), pPressed(false), key1Pressed(false), key2Pressed(false), key3Pressed(false), spacePressed(false),
        highScore(0), skinSelectIndex(0), selectingPlayer(1), p1Skin(SKIN_DEFAULT), p2Skin(SKIN_DEFAULT),
        diffLevel(DIFF_NORMAL), mapType(MAP_STAR), diffSelectIndex(1), mapSelectIndex(0),
        baseSpawnRate(g_diffSpawnRate[DIFF_NORMAL]), survivalTimer(0), survivalPlayerCount(1),
        showParticles(true), menuSel(0) {
        srand((unsigned)time(nullptr));
    }
    ~SpaceGame() { if (bg) delete bg; }

    void generateBackground() {
        if (bg) delete bg; bg = new IMAGE(SCREEN_WIDTH, SCREEN_HEIGHT); SetWorkingImage(bg);
        switch (mapType) {
        case MAP_STAR: setbkcolor(RGB(10, 10, 30)); cleardevice(); for (int i = 0; i < 300; i++) { int b = 100 + rand() % 156; setfillcolor(RGB(b, b, b)); solidcircle(rand() % SCREEN_WIDTH, rand() % SCREEN_HEIGHT, rand() % 2 + 1); } break;
        case MAP_DESERT: setbkcolor(RGB(200, 140, 60)); cleardevice(); setfillcolor(RGB(255, 200, 80)); solidcircle(SCREEN_WIDTH - 100, 80, 50); setfillcolor(RGB(255, 230, 150)); solidcircle(SCREEN_WIDTH - 100, 80, 35); setfillcolor(RGB(190, 130, 50)); for (int y = SCREEN_HEIGHT - 100; y < SCREEN_HEIGHT; y++) solidrectangle(0, y, SCREEN_WIDTH, y + 1); setfillcolor(RGB(160, 100, 30)); for (int y = SCREEN_HEIGHT - 40; y < SCREEN_HEIGHT; y++) solidrectangle(0, y, SCREEN_WIDTH, y + 1); for (int i = 0; i < 4; i++) { int cx = 100 + i * 180 + rand() % 60, cy = SCREEN_HEIGHT - 70 - rand() % 40; setfillcolor(RGB(50, 130, 50)); solidrectangle(cx - 3, cy - 30, cx + 3, cy); solidrectangle(cx - 15, cy - 20, cx - 3, cy - 15); solidrectangle(cx + 3, cy - 25, cx + 15, cy - 18); } break;
        case MAP_CYBER: setbkcolor(RGB(15, 0, 25)); cleardevice(); setlinecolor(RGB(0, 180, 180)); for (int i = 0; i < SCREEN_WIDTH; i += 30) line(i, 0, i, SCREEN_HEIGHT); for (int i = 0; i < SCREEN_HEIGHT; i += 30) line(0, i, SCREEN_WIDTH, i); for (int i = 0; i < 20; i++) { setfillcolor(RGB(0, 255, 100 + rand() % 100)); solidcircle(rand() % SCREEN_WIDTH, rand() % SCREEN_HEIGHT, 2 + rand() % 4); } setlinecolor(RGB(255, 0, 150)); for (int i = 0; i < 5; i++) { int x1 = rand() % SCREEN_WIDTH, y1 = rand() % SCREEN_HEIGHT, x2 = x1 + rand() % 200 - 100, y2 = y1 + rand() % 200 - 100; line(x1, y1, x2, y2); line(x2, y2, x2 + 30, y2 - 20); } break;
        }
        SetWorkingImage();
    }
    void init() { generateBackground(); }

    void spawnExplosion(double x, double y, COLORREF c, int n = 20) {
        if (!showParticles) return;
        for (int i = 0; i < n; i++) { Particle p; p.x = x; p.y = y; double a = (rand() % 360) * 3.14159 / 180.0, s = (rand() % 50 + 20) / 10.0; p.vx = cos(a) * s; p.vy = sin(a) * s; p.life = 1.0; p.color = c; p.size = rand() % 4 + 1; p.active = true; particles.push_back(p); }
    }
    void updateParticles() { for (auto& p : particles) { if (!p.active)continue; p.x += p.vx; p.y += p.vy; p.vx *= 0.98; p.vy *= 0.98; p.life -= 0.02; if (p.life <= 0)p.active = false; } particles.erase(remove_if(particles.begin(), particles.end(), [](Particle& p) {return !p.active; }), particles.end()); }
    void drawParticles() {
        if (!showParticles) return;
        for (auto& p : particles) { if (!p.active)continue; COLORREF c = RGB(min(255, int(GetRValue(p.color) * p.life * 1.2)), min(255, int(GetGValue(p.color) * p.life * 1.2)), min(255, int(GetBValue(p.color) * p.life * 1.2))); setfillcolor(c); solidcircle(int(p.x), int(p.y), p.size); }
    }

    void spawnEnemy() {
        if (enemies.size() >= maxEnemiesOnScreen) return;
        int t = rand() % 100;
        Enemy e(double(rand() % (SCREEN_WIDTH - 100) + 50), -30, t < 60 ? 0 : (t < 85 ? 1 : 2));
        e.speedX *= g_diffEnemySpeed[diffLevel]; e.speedY *= g_diffEnemySpeed[diffLevel];
        enemies.push_back(e);
    }
    void spawnBoss() {
        Enemy e(SCREEN_WIDTH / 2, -50, 3);
        e.speedX *= g_diffEnemySpeed[diffLevel]; e.speedY *= g_diffEnemySpeed[diffLevel];
        enemies.push_back(e);
    }
    void spawnPowerUp(double x, double y) { if (rand() % 100 < 15) powerUps.push_back(PowerUp(x, y, rand() % 3)); }

    void spawnEnemySurvival(Player* target) {
        if (enemies.size() >= MAX_ENEMIES || !target) return;
        double spawnX, spawnY, speed = 2.5 + difficulty * 0.3;
        int edge = rand() % 4;
        switch (edge) {
        case 0: spawnX = -30; spawnY = rand() % SCREEN_HEIGHT; break;
        case 1: spawnX = SCREEN_WIDTH + 30; spawnY = rand() % SCREEN_HEIGHT; break;
        case 2: spawnX = rand() % SCREEN_WIDTH; spawnY = -30; break;
        case 3: spawnX = rand() % SCREEN_WIDTH; spawnY = SCREEN_HEIGHT + 30; break;
        }
        double dx = target->x - spawnX, dy = target->y - spawnY, len = sqrt(dx * dx + dy * dy);
        if (len == 0) return;
        Enemy e(spawnX, spawnY, 0);
        e.speedX = dx / len * speed;
        e.speedY = dy / len * speed;
        e.movePattern = 2;
        e.shootCooldown = 40;
        enemies.push_back(e);
    }

    void playerShoot(Player& p) {
        if (!p.canShoot() || bullets.size() >= MAX_BULLETS) return;
        COLORREF bulletColor = p.getBulletColor();
        if (mode == MODE_SURVIVAL) {
            int count = 8; double speed = 5.0;
            if (p.weaponLevel == 2) { count = 12; speed = 5.5; }
            else if (p.weaponLevel >= 3) { count = 16; speed = 6.0; }
            for (int i = 0; i < count; i++) {
                double angle = i * 2 * 3.1415926535 / count;
                Bullet b(p.x, p.y, p.playerID, speed, 0.0, bulletColor);
                b.vx = cos(angle) * speed; b.vy = sin(angle) * speed;
                bullets.push_back(b);
            }
        }
        else {
            switch (p.weaponLevel) {
            case 1: bullets.push_back(Bullet(p.x, p.y - 20, p.playerID, 8.0, 0.0, bulletColor)); break;
            case 2: bullets.push_back(Bullet(p.x - 8, p.y - 15, p.playerID, 8.0, 0.0, bulletColor)); bullets.push_back(Bullet(p.x + 8, p.y - 15, p.playerID, 8.0, 0.0, bulletColor)); break;
            case 3: bullets.push_back(Bullet(p.x, p.y - 20, p.playerID, 10.0, 0.0, bulletColor)); bullets.push_back(Bullet(p.x - 10, p.y - 15, p.playerID, 7.0, -1.0, bulletColor)); bullets.push_back(Bullet(p.x + 10, p.y - 15, p.playerID, 7.0, 1.0, bulletColor)); break;
            }
        }
    }
    void enemyShoot(Enemy& e, Player& t) {
        if (bullets.size() >= MAX_BULLETS) return;
        double dx = t.x - e.x, dy = t.y - e.y, d = sqrt(dx * dx + dy * dy);
        if (d > 0) { Bullet b(e.x, e.y + 15, 0); b.vx = dx / d * 3; b.vy = dy / d * 3; bullets.push_back(b); }
    }

    void updateLasers() { for (auto& l : lasers) l.update(); lasers.erase(remove_if(lasers.begin(), lasers.end(), [](Laser& l) {return !l.active; }), lasers.end()); }
    void handleLaserCollisions() {
        for (auto& l : lasers) {
            if (player1.active && l.checkCollision(player1) && player1.invincibleTimer <= 0) { player1.lives--; if (player1.lives <= 0) player1.active = false; player1.invincibleTimer = 90; player1.combo = 0; spawnExplosion(player1.x, player1.y, player1.skin.bodyColor, 30); }
            if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player2.active && l.checkCollision(player2) && player2.invincibleTimer <= 0) { player2.lives--; if (player2.lives <= 0) player2.active = false; player2.invincibleTimer = 90; player2.combo = 0; spawnExplosion(player2.x, player2.y, player2.skin.bodyColor, 30); }
        }
    }

    void handleCollisions() {
        for (auto& b : bullets) {
            if (!b.active || b.playerID == 0) continue;
            for (auto& e : enemies) {
                if (!e.active) continue;
                double d = sqrt(pow(b.x - e.x, 2) + pow(b.y - e.y, 2)); int hr = e.type == 3 ? 25 : (e.type == 2 ? 18 : 15);
                if (d < hr) {
                    b.active = false; e.health -= b.damage; spawnExplosion(b.x, b.y, RGB(255, 200, 100), 5);
                    if (e.health <= 0) {
                        spawnExplosion(e.x, e.y, e.color, e.type == 3 ? 60 : (e.type == 2 ? 40 : 20)); spawnPowerUp(e.x, e.y); e.active = false;
                        Player& shooter = (b.playerID == 1) ? player1 : player2; shooter.combo++; shooter.comboTime = 60; int pts = 10 * (e.type + 1) * shooter.combo; shooter.score += pts; totalScore += pts;
                        if (shooter.score > shooter.weaponLevel * 500) shooter.weaponLevel = min(3, shooter.weaponLevel + 1);
                    } break;
                }
            }
        }
        for (auto& b : bullets) {
            if (!b.active || b.playerID != 0) continue;
            if (player1.active && sqrt(pow(b.x - player1.x, 2) + pow(b.y - player1.y, 2)) < 15 && player1.invincibleTimer <= 0) { b.active = false; player1.lives--; if (player1.lives <= 0) player1.active = false; player1.invincibleTimer = 90; player1.combo = 0; spawnExplosion(player1.x, player1.y, player1.skin.bodyColor, 30); }
            if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player2.active && sqrt(pow(b.x - player2.x, 2) + pow(b.y - player2.y, 2)) < 15 && player2.invincibleTimer <= 0) { b.active = false; player2.lives--; if (player2.lives <= 0) player2.active = false; player2.invincibleTimer = 90; player2.combo = 0; spawnExplosion(player2.x, player2.y, player2.skin.bodyColor, 30); }
        }
        for (auto& e : enemies) {
            if (!e.active) continue;
            if (player1.active && sqrt(pow(e.x - player1.x, 2) + pow(e.y - player1.y, 2)) < 25 && player1.invincibleTimer <= 0) { e.active = false; player1.lives--; if (player1.lives <= 0) player1.active = false; player1.invincibleTimer = 90; player1.combo = 0; spawnExplosion(e.x, e.y, RGB(255, 50, 50), 25); spawnExplosion(player1.x, player1.y, player1.skin.bodyColor, 20); }
            if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player2.active && sqrt(pow(e.x - player2.x, 2) + pow(e.y - player2.y, 2)) < 25 && player2.invincibleTimer <= 0) { e.active = false; player2.lives--; if (player2.lives <= 0) player2.active = false; player2.invincibleTimer = 90; player2.combo = 0; spawnExplosion(e.x, e.y, RGB(255, 50, 50), 25); spawnExplosion(player2.x, player2.y, player2.skin.bodyColor, 20); }
        }
        for (auto& pu : powerUps) {
            if (!pu.active) continue;
            if (player1.active && sqrt(pow(pu.x - player1.x, 2) + pow(pu.y - player1.y, 2)) < 20) { applyPowerUp(player1, pu); pu.active = false; }
            if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player2.active && sqrt(pow(pu.x - player2.x, 2) + pow(pu.y - player2.y, 2)) < 20) { applyPowerUp(player2, pu); pu.active = false; }
        }
        handleLaserCollisions();
        if (mode == MODE_SURVIVAL) {
            if ((!player1.active && (survivalPlayerCount == 1 || !player2.active))) state = STATE_SURVIVAL_FAIL;
            else if (survivalTimer <= 0) state = STATE_SURVIVAL_SUCCESS;
        }
        else {
            if (!player1.active && (mode == MODE_SINGLE || !player2.active)) { state = STATE_GAMEOVER; if (totalScore > highScore) highScore = totalScore; }
        }
        bullets.erase(remove_if(bullets.begin(), bullets.end(), [](Bullet& b) {return !b.active; }), bullets.end());
        enemies.erase(remove_if(enemies.begin(), enemies.end(), [](Enemy& e) {return !e.active; }), enemies.end());
        powerUps.erase(remove_if(powerUps.begin(), powerUps.end(), [](PowerUp& p) {return !p.active; }), powerUps.end());
    }
    void applyPowerUp(Player& p, PowerUp& pu) { switch (pu.type) { case 0: p.lives = min(5, p.lives + 1); break; case 1: p.weaponLevel = min(3, p.weaponLevel + 1); break; case 2: p.invincibleTimer = 180; break; } }

#define IS_UP (g_keys[VK_UP]||g_keys['W']||g_keys[VK_NUMPAD8])
#define IS_DOWN (g_keys[VK_DOWN]||g_keys['S']||g_keys[VK_NUMPAD2])
#define IS_LEFT (g_keys[VK_LEFT]||g_keys['A']||g_keys[VK_NUMPAD4])
#define IS_RIGHT (g_keys[VK_RIGHT]||g_keys['D']||g_keys[VK_NUMPAD6])
#define IS_CONFIRM (g_keys[VK_SPACE]||g_keys[VK_RETURN])

                                                                         void update() {
                                                                             MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { if (msg.message == WM_QUIT) exit(0); TranslateMessage(&msg); DispatchMessage(&msg); }

                                                                             // ★ 状态变化时，将所有按键锁存标志置为 true，避免穿透
                                                                             if (state != prevState) {
                                                                                 enterPressed = escPressed = pPressed = key1Pressed = key2Pressed = key3Pressed = spacePressed = true;
                                                                                 prevState = state;
                                                                             }

                                                                             if (state == STATE_MENU) { handleMenuInput(); return; }
                                                                             if (state == STATE_MODE_SELECT) { handleModeSelect(); return; }
                                                                             if (state == STATE_SURVIVAL_MODE_SELECT) { handleSurvivalModeSelect(); return; }
                                                                             if (state == STATE_DIFF_SELECT) { handleDiffSelect(); return; }
                                                                             if (state == STATE_MAP_SELECT) { handleMapSelect(); return; }
                                                                             if (state == STATE_SKIN_SELECT) { handleSkinSelect(); return; }
                                                                             if (state == STATE_HELP) { if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MENU; } if (!g_keys[VK_ESCAPE]) escPressed = false; return; }
                                                                             if (state == STATE_SETTINGS) { handleSettingsInput(); return; }
                                                                             if (state == STATE_GAMEOVER || state == STATE_SURVIVAL_SUCCESS || state == STATE_SURVIVAL_FAIL) { handleResultInput(); return; }

                                                                             if (g_keys['P'] && !pPressed) { pPressed = true; state = (state == STATE_PLAYING) ? STATE_PAUSED : STATE_PLAYING; } if (!g_keys['P']) pPressed = false;
                                                                             if (state != STATE_PLAYING) return;

                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MENU; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;

                                                                             if (player1.active) {
                                                                                 if (g_keys['A'])player1.moveLeft(); if (g_keys['D'])player1.moveRight();
                                                                                 if (g_keys['W'])player1.moveUp(); if (g_keys['S'])player1.moveDown();
                                                                                 if (g_keys[VK_SPACE])playerShoot(player1);
                                                                             }
                                                                             if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player2.active) {
                                                                                 if (g_keys[VK_LEFT])player2.moveLeft(); if (g_keys[VK_RIGHT])player2.moveRight();
                                                                                 if (g_keys[VK_UP])player2.moveUp(); if (g_keys[VK_DOWN])player2.moveDown();
                                                                                 if (g_keys[VK_RETURN])playerShoot(player2);
                                                                             }
                                                                             player1.update(); if (mode == MODE_COOP || mode == MODE_SURVIVAL) player2.update();

                                                                             if (mode == MODE_SURVIVAL && survivalTimer > 0) survivalTimer--;

                                                                             for (auto& b : bullets) b.update();
                                                                             for (auto& e : enemies) {
                                                                                 e.update();
                                                                                 if (e.canShoot()) {
                                                                                     if (e.fireLaser && lasers.size() < MAX_LASERS) {
                                                                                         Player* target = nullptr;
                                                                                         if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player1.active && player2.active) target = (rand() % 2) ? &player1 : &player2;
                                                                                         else if (player1.active) target = &player1;
                                                                                         else if (player2.active) target = &player2;
                                                                                         if (target) { lasers.push_back(Laser()); lasers.back().activate(e.x, e.y + 25, target->x, target->y); }
                                                                                     }
                                                                                     if ((mode == MODE_COOP || mode == MODE_SURVIVAL) && player1.active && player2.active) enemyShoot(e, rand() % 2 ? player1 : player2);
                                                                                     else if (player1.active) enemyShoot(e, player1);
                                                                                     else if (player2.active) enemyShoot(e, player2);
                                                                                 }
                                                                             }
                                                                             for (auto& pu : powerUps) pu.update();
                                                                             updateLasers();
                                                                             updateParticles();
                                                                             handleCollisions();

                                                                             if (mode == MODE_SURVIVAL) {
                                                                                 if (enemySpawnTimer++ >= max(10, 25 - difficulty * 1)) {
                                                                                     Player* target = nullptr;
                                                                                     if ((mode == MODE_SURVIVAL || mode == MODE_COOP) && player1.active && player2.active) target = (rand() % 2) ? &player1 : &player2;
                                                                                     else if (player1.active) target = &player1;
                                                                                     else if (player2.active) target = &player2;
                                                                                     if (target) spawnEnemySurvival(target);
                                                                                     enemySpawnTimer = 0;
                                                                                 }
                                                                                 if (survivalTimer % (60 * 30) == 0 && survivalTimer > 0) difficulty++;
                                                                             }
                                                                             else {
                                                                                 enemySpawnTimer++;
                                                                                 if (enemySpawnTimer >= max(10, (int)(baseSpawnRate - difficulty * 1.5))) { spawnEnemy(); enemySpawnTimer = 0; }
                                                                                 bossSpawnTimer++;
                                                                                 if (bossSpawnTimer >= (mode == MODE_COOP ? 700 : 1100)) { spawnBoss(); bossSpawnTimer = 0; }
                                                                                 if (totalScore > difficulty * 1500) { difficulty++; maxEnemiesOnScreen = min(MAX_ENEMIES, g_diffStartEnemies[diffLevel] + difficulty * 2); }
                                                                             }
                                                                         }

                                                                         void handleMenuInput() {
                                                                             if (g_keys[VK_RETURN] && !enterPressed) { enterPressed = true; state = STATE_MODE_SELECT; menuSel = 0; }
                                                                             if (!g_keys[VK_RETURN]) enterPressed = false;
                                                                             if (g_keys['H'] && !key1Pressed) { key1Pressed = true; state = STATE_HELP; }
                                                                             if (!g_keys['H']) key1Pressed = false;
                                                                             if (g_keys['S'] && !key1Pressed) { key1Pressed = true; state = STATE_SETTINGS; menuSel = 0; }
                                                                             if (!g_keys['S']) key1Pressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; exit(0); }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleSettingsInput() {
                                                                             if (IS_UP) { if (!key1Pressed) { key1Pressed = true; menuSel = (menuSel - 1 + 2) % 2; } }
                                                                             else key1Pressed = false;
                                                                             if (IS_DOWN) { if (!key2Pressed) { key2Pressed = true; menuSel = (menuSel + 1) % 2; } }
                                                                             else key2Pressed = false;
                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 if (menuSel == 0) showParticles = !showParticles;
                                                                                 else state = STATE_MENU;
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MENU; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleModeSelect() {
                                                                             if (IS_UP) { if (!key1Pressed) { key1Pressed = true; menuSel = (menuSel - 1 + 3) % 3; } }
                                                                             else key1Pressed = false;
                                                                             if (IS_DOWN) { if (!key2Pressed) { key2Pressed = true; menuSel = (menuSel + 1) % 3; } }
                                                                             else key2Pressed = false;
                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 if (menuSel == 0) { mode = MODE_SINGLE; state = STATE_DIFF_SELECT; diffSelectIndex = 1; menuSel = 1; }
                                                                                 else if (menuSel == 1) { mode = MODE_COOP; state = STATE_DIFF_SELECT; diffSelectIndex = 1; menuSel = 1; }
                                                                                 else if (menuSel == 2) { state = STATE_SURVIVAL_MODE_SELECT; menuSel = 0; }
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MENU; menuSel = 0; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleSurvivalModeSelect() {
                                                                             if (IS_UP) { if (!key1Pressed) { key1Pressed = true; menuSel = (menuSel - 1 + 2) % 2; } }
                                                                             else key1Pressed = false;
                                                                             if (IS_DOWN) { if (!key2Pressed) { key2Pressed = true; menuSel = (menuSel + 1) % 2; } }
                                                                             else key2Pressed = false;
                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 if (menuSel == 0) { mode = MODE_SURVIVAL; survivalPlayerCount = 1; state = STATE_DIFF_SELECT; diffSelectIndex = 1; menuSel = 1; }
                                                                                 else if (menuSel == 1) { mode = MODE_SURVIVAL; survivalPlayerCount = 2; state = STATE_DIFF_SELECT; diffSelectIndex = 1; menuSel = 1; }
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MODE_SELECT; menuSel = 0; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleDiffSelect() {
                                                                             if (IS_UP) { if (!key1Pressed) { key1Pressed = true; diffSelectIndex = (diffSelectIndex - 1 + DIFF_COUNT) % DIFF_COUNT; } }
                                                                             else key1Pressed = false;
                                                                             if (IS_DOWN) { if (!key2Pressed) { key2Pressed = true; diffSelectIndex = (diffSelectIndex + 1) % DIFF_COUNT; } }
                                                                             else key2Pressed = false;
                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 diffLevel = (DifficultyLevel)diffSelectIndex; baseSpawnRate = g_diffSpawnRate[diffLevel];
                                                                                 maxEnemiesOnScreen = (mode == MODE_SURVIVAL) ? MAX_ENEMIES : g_diffStartEnemies[diffLevel];
                                                                                 state = STATE_MAP_SELECT; mapSelectIndex = 0; menuSel = 0;
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = (mode == MODE_SURVIVAL) ? STATE_SURVIVAL_MODE_SELECT : STATE_MODE_SELECT; menuSel = 0; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleMapSelect() {
                                                                             if (IS_UP) { if (!key1Pressed) { key1Pressed = true; mapSelectIndex = (mapSelectIndex - 1 + MAP_COUNT) % MAP_COUNT; } }
                                                                             else key1Pressed = false;
                                                                             if (IS_DOWN) { if (!key2Pressed) { key2Pressed = true; mapSelectIndex = (mapSelectIndex + 1) % MAP_COUNT; } }
                                                                             else key2Pressed = false;
                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 mapType = (MapType)mapSelectIndex; generateBackground();
                                                                                 state = STATE_SKIN_SELECT; selectingPlayer = 1; skinSelectIndex = 0;
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_DIFF_SELECT; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleSkinSelect() {
                                                                             if (IS_LEFT && !key1Pressed) { key1Pressed = true; skinSelectIndex = (skinSelectIndex - 1 + SKIN_COUNT) % SKIN_COUNT; }
                                                                             else if (!IS_LEFT) key1Pressed = false;
                                                                             if (IS_RIGHT && !key2Pressed) { key2Pressed = true; skinSelectIndex = (skinSelectIndex + 1) % SKIN_COUNT; }
                                                                             else if (!IS_RIGHT) key2Pressed = false;

                                                                             if (IS_CONFIRM && !spacePressed) {
                                                                                 spacePressed = true;
                                                                                 if (selectingPlayer == 1) {
                                                                                     player1.setSkin(skinSelectIndex); p1Skin = skinSelectIndex;
                                                                                     if (mode == MODE_COOP || (mode == MODE_SURVIVAL && survivalPlayerCount == 2)) { selectingPlayer = 2; skinSelectIndex = 0; }
                                                                                     else startNewGame();
                                                                                 }
                                                                                 else { player2.setSkin(skinSelectIndex); p2Skin = skinSelectIndex; startNewGame(); }
                                                                             }
                                                                             if (!IS_CONFIRM) spacePressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MAP_SELECT; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }
                                                                         void handleResultInput() {
                                                                             if (g_keys[VK_RETURN] && !enterPressed) { enterPressed = true; state = STATE_MODE_SELECT; menuSel = 0; }
                                                                             if (!g_keys[VK_RETURN]) enterPressed = false;
                                                                             if (g_keys[VK_ESCAPE] && !escPressed) { escPressed = true; state = STATE_MENU; }
                                                                             if (!g_keys[VK_ESCAPE]) escPressed = false;
                                                                         }

                                                                         void startNewGame() {
                                                                             player1 = Player(1); player1.setSkin(p1Skin);
                                                                             player2 = Player(2); player2.setSkin(p2Skin);
                                                                             if (mode == MODE_SURVIVAL && survivalPlayerCount == 1) player2.active = false;
                                                                             bullets.clear(); enemies.clear(); particles.clear(); powerUps.clear(); lasers.clear();
                                                                             difficulty = 1;
                                                                             if (mode == MODE_SURVIVAL) { maxEnemiesOnScreen = MAX_ENEMIES; survivalTimer = SURVIVAL_DURATION; }
                                                                             enemySpawnTimer = 0; bossSpawnTimer = 0; totalScore = 0; state = STATE_PLAYING;
                                                                         }

                                                                         void drawBreadcrumb(const TCHAR* path) { settextstyle(18, 0, _T("Arial")); settextcolor(RGB(150, 150, 150)); outtextxy(10, 10, path); }

                                                                         void render() {
                                                                             BeginBatchDraw(); if (bg) putimage(0, 0, bg);
                                                                             if (state == STATE_MENU) { drawBreadcrumb(_T("主菜单")); drawMenu(); }
                                                                             else if (state == STATE_MODE_SELECT) { drawBreadcrumb(_T("主菜单 > 模式选择")); drawModeSelect(); }
                                                                             else if (state == STATE_SURVIVAL_MODE_SELECT) { drawBreadcrumb(_T("主菜单 > 模式选择 > 生存人数")); drawSurvivalModeSelect(); }
                                                                             else if (state == STATE_DIFF_SELECT) {
                                                                                 TCHAR path[100]; if (mode == MODE_SURVIVAL) _stprintf_s(path, _T("主菜单 > 模式选择 > 生存人数 > 难度选择")); else _stprintf_s(path, _T("主菜单 > 模式选择 > 难度选择"));
                                                                                 drawBreadcrumb(path); drawDiffSelect();
                                                                             }
                                                                             else if (state == STATE_MAP_SELECT) {
                                                                                 TCHAR path[100]; if (mode == MODE_SURVIVAL) _stprintf_s(path, _T("主菜单 > 模式选择 > 生存人数 > 难度选择 > 地图选择")); else _stprintf_s(path, _T("主菜单 > 模式选择 > 难度选择 > 地图选择"));
                                                                                 drawBreadcrumb(path); drawMapSelect();
                                                                             }
                                                                             else if (state == STATE_SKIN_SELECT) {
                                                                                 TCHAR path[100]; if (mode == MODE_SURVIVAL) _stprintf_s(path, _T("主菜单 > 模式选择 > 生存人数 > 难度选择 > 地图选择 > 皮肤选择")); else _stprintf_s(path, _T("主菜单 > 模式选择 > 难度选择 > 地图选择 > 皮肤选择"));
                                                                                 drawBreadcrumb(path); drawSkinSelect();
                                                                             }
                                                                             else if (state == STATE_HELP) { drawBreadcrumb(_T("主菜单 > 帮助")); drawHelp(); }
                                                                             else if (state == STATE_SETTINGS) { drawBreadcrumb(_T("主菜单 > 设置")); drawSettings(); }
                                                                             else if (state == STATE_PLAYING || state == STATE_PAUSED) {
                                                                                 for (auto& pu : powerUps) pu.draw(); for (auto& b : bullets) b.draw(); for (auto& e : enemies) e.draw(); for (auto& l : lasers) l.draw();
                                                                                 player1.draw(); if (mode == MODE_COOP || (mode == MODE_SURVIVAL && survivalPlayerCount == 2)) player2.draw(); drawParticles(); drawUI();
                                                                                 if (state == STATE_PAUSED) { drawPause(); }
                                                                             }
                                                                             else if (state == STATE_GAMEOVER) drawGameOver();
                                                                             else if (state == STATE_SURVIVAL_SUCCESS) drawSurvivalResult(true);
                                                                             else if (state == STATE_SURVIVAL_FAIL) drawSurvivalResult(false);
                                                                             EndBatchDraw();
                                                                         }

                                                                         void drawMenu() {
                                                                             setbkmode(TRANSPARENT); settextstyle(55, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 200, 100, _T("太空射击游戏"));
                                                                             settextstyle(25, 0, _T("Arial")); settextcolor(RGB(255, 255, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 160, 240, _T("按 ENTER 开始")); outtextxy(SCREEN_WIDTH / 2 - 140, 280, _T("按 S 设置"));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 120, 320, _T("按 H 帮助")); outtextxy(SCREEN_WIDTH / 2 - 110, 360, _T("按 ESC 退出"));
                                                                             TCHAR t[50]; _stprintf_s(t, _T("最高分: %d"), highScore); settextcolor(RGB(255, 215, 0)); outtextxy(SCREEN_WIDTH / 2 - 70, 420, t);
                                                                         }
                                                                         void drawSettings() {
                                                                             setbkmode(TRANSPARENT); setfillcolor(RGB(0, 0, 0)); solidrectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
                                                                             settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255)); outtextxy(SCREEN_WIDTH / 2 - 60, 150, _T("设置"));
                                                                             settextstyle(25, 0, _T("Arial"));
                                                                             const TCHAR* opts[] = { _T("粒子特效"), _T("返回") };
                                                                             for (int i = 0; i < 2; i++) {
                                                                                 TCHAR buf[40];
                                                                                 if (i == 0) _stprintf_s(buf, _T("%s: %s"), opts[i], showParticles ? _T("开启") : _T("关闭"));
                                                                                 else _stprintf_s(buf, _T("%s"), opts[i]);
                                                                                 settextcolor(i == menuSel ? RGB(255, 255, 0) : RGB(255, 255, 255));
                                                                                 outtextxy(SCREEN_WIDTH / 2 - 100, 260 + i * 60, buf);
                                                                             }
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 150, 450, _T("W/S 移动 空格/回车 切换  ESC 返回"));
                                                                         }
                                                                         void drawHelp() {
                                                                             setfillcolor(RGB(0, 0, 0)); solidrectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); setbkmode(TRANSPARENT);
                                                                             settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255)); outtextxy(SCREEN_WIDTH / 2 - 50, 30, _T("帮助"));
                                                                             settextstyle(22, 0, _T("Arial")); settextcolor(RGB(255, 255, 255));
                                                                             outtextxy(80, 100, _T("【操作说明】")); outtextxy(80, 140, _T("玩家1: W/A/S/D 移动, 空格射击"));
                                                                             outtextxy(80, 175, _T("玩家2: 方向键 移动, 回车射击 (双人)"));
                                                                             outtextxy(80, 210, _T("P 暂停, ESC 返回, H 帮助"));
                                                                             outtextxy(80, 260, _T("【道具说明】")); outtextxy(80, 300, _T("红色十字: 生命+1"));
                                                                             outtextxy(80, 335, _T("金色星星: 武器升级")); outtextxy(80, 370, _T("蓝色菱形: 3秒护盾"));
                                                                             settextstyle(25, 0, _T("Arial")); settextcolor(RGB(255, 215, 0));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 120, 450, _T("制作人：rongzhong6  fufu"));
                                                                             settextstyle(16, 0, _T("Arial")); settextcolor(RGB(180, 180, 180));
                                                                             TCHAR ver[30]; _stprintf_s(ver, _T("版本：%s"), GAME_VERSION); outtextxy(SCREEN_WIDTH - 120, SCREEN_HEIGHT - 30, ver);
                                                                         }
                                                                         void drawModeSelect() {
                                                                             setbkmode(TRANSPARENT); settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 160, 150, _T("选择模式")); settextstyle(25, 0, _T("Arial"));
                                                                             const TCHAR* modes[] = { _T("单人闯关"), _T("双人闯关"), _T("生存挑战") };
                                                                             for (int i = 0; i < 3; i++) { settextcolor(i == menuSel ? RGB(255, 255, 0) : RGB(255, 255, 255)); outtextxy(SCREEN_WIDTH / 2 - 100, 260 + i * 60, modes[i]); }
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200)); outtextxy(SCREEN_WIDTH / 2 - 130, 500, _T("W/S 移动 空格/回车 确认  ESC 返回"));
                                                                         }
                                                                         void drawSurvivalModeSelect() {
                                                                             setbkmode(TRANSPARENT); settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 160, 150, _T("生存挑战 - 选择人数")); settextstyle(25, 0, _T("Arial"));
                                                                             const TCHAR* opts[] = { _T("单人"), _T("双人") };
                                                                             for (int i = 0; i < 2; i++) { settextcolor(i == menuSel ? RGB(255, 255, 0) : RGB(255, 255, 255)); outtextxy(SCREEN_WIDTH / 2 - 100, 260 + i * 60, opts[i]); }
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200)); outtextxy(SCREEN_WIDTH / 2 - 130, 500, _T("W/S 移动 空格/回车 确认  ESC 返回"));
                                                                         }
                                                                         void drawDiffSelect() {
                                                                             setbkmode(TRANSPARENT); settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 140, 150, _T("选择难度")); settextstyle(25, 0, _T("Arial"));
                                                                             for (int i = 0; i < DIFF_COUNT; i++) { TCHAR buf[30]; _stprintf_s(buf, _T("%s"), g_diffNames[i]); settextcolor(i == diffSelectIndex ? RGB(255, 255, 0) : RGB(255, 255, 255)); outtextxy(SCREEN_WIDTH / 2 - 100, 260 + i * 60, buf); }
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200)); outtextxy(SCREEN_WIDTH / 2 - 130, 500, _T("W/S 移动 空格/回车 确认  ESC 返回"));
                                                                         }
                                                                         void drawMapSelect() {
                                                                             setbkmode(TRANSPARENT); settextstyle(35, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 140, 150, _T("选择地图")); settextstyle(25, 0, _T("Arial"));
                                                                             for (int i = 0; i < MAP_COUNT; i++) { TCHAR buf[30]; _stprintf_s(buf, _T("%s"), g_mapNames[i]); settextcolor(i == mapSelectIndex ? RGB(255, 255, 0) : RGB(255, 255, 255)); outtextxy(SCREEN_WIDTH / 2 - 100, 260 + i * 60, buf); }
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200)); outtextxy(SCREEN_WIDTH / 2 - 130, 500, _T("W/S 移动 空格/回车 确认  ESC 返回"));
                                                                         }
                                                                         void drawSkinSelect() {
                                                                             setbkmode(TRANSPARENT); settextstyle(30, 0, _T("Arial Black")); settextcolor(RGB(0, 200, 255));
                                                                             TCHAR title[50]; _stprintf_s(title, _T("选择皮肤 - 玩家 %d"), selectingPlayer); outtextxy(SCREEN_WIDTH / 2 - 140, 50, title);
                                                                             Player preview(selectingPlayer); preview.setSkin(skinSelectIndex); preview.x = SCREEN_WIDTH / 2; preview.y = 200; preview.draw();
                                                                             settextstyle(25, 0, _T("Arial")); settextcolor(RGB(255, 215, 0));
                                                                             TCHAR name[30]; _stprintf_s(name, _T("<< %s >>"), g_skins[skinSelectIndex].name); outtextxy(SCREEN_WIDTH / 2 - 60, 280, name);
                                                                             settextstyle(18, 0, _T("Arial")); settextcolor(RGB(200, 200, 200));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 170, 350, _T("A/D 或 ← → 切换  空格/回车 确认  ESC 返回"));
                                                                             settextstyle(16, 0, _T("Arial"));
                                                                             int startX = SCREEN_WIDTH / 2 - 280;
                                                                             for (int i = 0; i < SKIN_COUNT; i++) { int x = startX + i * 95; settextcolor(i == skinSelectIndex ? RGB(255, 255, 0) : RGB(150, 150, 150)); TCHAR mark[4]; _stprintf_s(mark, i == skinSelectIndex ? _T("[*]") : _T("[ ]")); outtextxy(x, 500, mark); settextcolor(RGB(255, 255, 255)); outtextxy(x + 20, 500, g_skins[i].name); }
                                                                         }
                                                                         void drawUI() {
                                                                             setbkmode(TRANSPARENT); settextstyle(20, 0, _T("Arial")); TCHAR t[50];
                                                                             settextcolor(RGB(0, 200, 255)); _stprintf_s(t, _T("P1:%d 生命:%d"), player1.score, player1.lives); outtextxy(20, 20, t);
                                                                             if (mode == MODE_COOP || (mode == MODE_SURVIVAL && survivalPlayerCount == 2)) { settextcolor(RGB(100, 255, 100)); _stprintf_s(t, _T("P2:%d 生命:%d"), player2.score, player2.lives); outtextxy(SCREEN_WIDTH - 180, 20, t); }
                                                                             if (mode == MODE_SURVIVAL) {
                                                                                 int sec = survivalTimer / 60;
                                                                                 settextcolor(RGB(255, 255, 0)); _stprintf_s(t, _T("剩余时间: %d:%02d"), sec / 60, sec % 60); outtextxy(SCREEN_WIDTH / 2 - 90, 40, t);
                                                                             }
                                                                             else {
                                                                                 settextcolor(RGB(255, 255, 255)); _stprintf_s(t, _T("总分:%d 等级:%d"), totalScore, difficulty); outtextxy(SCREEN_WIDTH / 2 - 90, 20, t);
                                                                                 settextcolor(RGB(255, 215, 0)); _stprintf_s(t, _T("武器 Lv.%d"), player1.weaponLevel); outtextxy(20, 60, t);
                                                                                 if (mode == MODE_COOP) { _stprintf_s(t, _T("武器 Lv.%d"), player2.weaponLevel); outtextxy(SCREEN_WIDTH - 180, 60, t); }
                                                                             }
                                                                             settextcolor(RGB(150, 150, 150)); _stprintf_s(t, _T("皮肤:%s"), player1.skin.name); outtextxy(20, 85, t);
                                                                             if (mode == MODE_COOP || (mode == MODE_SURVIVAL && survivalPlayerCount == 2)) { _stprintf_s(t, _T("皮肤:%s"), player2.skin.name); outtextxy(SCREEN_WIDTH - 180, 85, t); }
                                                                         }
                                                                         void drawPause() { setfillcolor(RGB(0, 0, 0)); solidrectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); setbkmode(TRANSPARENT); settextstyle(50, 0, _T("Arial Black")); settextcolor(RGB(255, 255, 0)); outtextxy(SCREEN_WIDTH / 2 - 100, SCREEN_HEIGHT / 2 - 30, _T("暂停中")); settextstyle(20, 0, _T("Arial")); settextcolor(RGB(255, 255, 255)); outtextxy(SCREEN_WIDTH / 2 - 110, SCREEN_HEIGHT / 2 + 30, _T("按 P 继续")); }
                                                                         void drawGameOver() {
                                                                             setfillcolor(RGB(0, 0, 0)); solidrectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); setbkmode(TRANSPARENT);
                                                                             settextstyle(55, 0, _T("Arial Black")); settextcolor(RGB(255, 50, 50)); outtextxy(SCREEN_WIDTH / 2 - 150, 150, _T("游戏结束"));
                                                                             TCHAR t[80]; settextstyle(25, 0, _T("Arial")); settextcolor(RGB(255, 255, 255));
                                                                             _stprintf_s(t, _T("P1:%d 生命:%d"), player1.score, player1.lives); outtextxy(SCREEN_WIDTH / 2 - 150, 250, t);
                                                                             if (mode == MODE_COOP) { _stprintf_s(t, _T("P2:%d 生命:%d"), player2.score, player2.lives); outtextxy(SCREEN_WIDTH / 2 - 150, 280, t); }
                                                                             _stprintf_s(t, _T("总分:%d"), totalScore); settextcolor(RGB(255, 215, 0)); outtextxy(SCREEN_WIDTH / 2 - 60, 330, t);
                                                                             if (totalScore >= highScore && totalScore > 0) { settextcolor(RGB(255, 255, 0)); outtextxy(SCREEN_WIDTH / 2 - 100, 360, _T("新最高分!")); }
                                                                             settextstyle(20, 0, _T("Arial")); settextcolor(RGB(255, 255, 255));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 130, 430, _T("ENTER 再来")); outtextxy(SCREEN_WIDTH / 2 - 120, 460, _T("ESC 菜单"));
                                                                         }
                                                                         void drawSurvivalResult(bool success) {
                                                                             setfillcolor(RGB(0, 0, 0)); solidrectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT); setbkmode(TRANSPARENT);
                                                                             settextstyle(55, 0, _T("Arial Black"));
                                                                             if (success) { settextcolor(RGB(0, 255, 0)); outtextxy(SCREEN_WIDTH / 2 - 180, 200, _T("挑战成功！")); }
                                                                             else { settextcolor(RGB(255, 50, 50)); outtextxy(SCREEN_WIDTH / 2 - 180, 200, _T("挑战失败！")); }
                                                                             settextstyle(25, 0, _T("Arial")); settextcolor(RGB(255, 255, 255));
                                                                             TCHAR t[80]; _stprintf_s(t, _T("存活时间: %d:%02d"), (SURVIVAL_DURATION - survivalTimer) / 3600, ((SURVIVAL_DURATION - survivalTimer) / 60) % 60);
                                                                             outtextxy(SCREEN_WIDTH / 2 - 100, 300, t);
                                                                             _stprintf_s(t, _T("击杀得分: %d"), totalScore); outtextxy(SCREEN_WIDTH / 2 - 80, 340, t);
                                                                             settextstyle(20, 0, _T("Arial")); settextcolor(RGB(200, 200, 200));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 130, 430, _T("ENTER 返回模式选择"));
                                                                             outtextxy(SCREEN_WIDTH / 2 - 120, 460, _T("ESC 返回主菜单"));
                                                                         }
                                                                         void run() { init(); while (1) { update(); render(); Sleep(16); } }
};

int main() {
    initgraph(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetWindowText(GetHWnd(), _T("太空射击游戏 V1.5"));
    SetWindowLongPtr(GetHWnd(), GWLP_WNDPROC, (LONG_PTR)GameWndProc);
    SpaceGame game; game.run();
    closegraph(); return 0;
}
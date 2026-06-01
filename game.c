// ============================================================================
//  game.c — A tiny native first-person shooter built with raylib.
//
//  Controls:
//    Mouse        - look around
//    W A S D      - move
//    Left mouse   - shoot
//    Spacebar     - shoot
//    ESC          - quit (and give your mouse cursor back)
//
//  How it works at a high level:
//    raylib gives us a window, a 3D camera, input handling, and simple shape
//    drawing. We keep an array of "enemy" cubes. Every frame we (1) update the
//    camera from mouse + WASD, (2) check if the player fired and, if so, cast a
//    ray from the camera through the crosshair to see which cube it hits, then
//    (3) draw the 3D world followed by the 2D HUD on top.
// ============================================================================

#include "raylib.h"
#include "raymath.h"   // Vector helpers (not strictly required, handy to have)
#include <stdlib.h>    // rand(), srand()
#include <time.h>      // time() to seed the random number generator

// ---- Tunable constants -----------------------------------------------------
#define SCREEN_WIDTH   1280
#define SCREEN_HEIGHT  720
#define ARENA_HALF     16.0f   // Arena spans -ARENA_HALF..+ARENA_HALF on X and Z
#define WALL_HEIGHT    4.0f
#define WALL_THICK     1.0f
#define ENEMY_COUNT    8       // How many target cubes exist at once
#define ENEMY_SIZE     2.0f    // Cube edge length

// An enemy is a position, a velocity (so it can wander), and an alive flag.
typedef struct Enemy {
    Vector3 position;
    Vector3 velocity;   // units per second; only X and Z are used (cubes slide on the floor)
    bool    alive;
} Enemy;

// ---- Helpers ---------------------------------------------------------------

// Return a random float in [min, max]. rand() gives an int in [0, RAND_MAX].
static float RandFloat(float min, float max) {
    float t = (float)rand() / (float)RAND_MAX;   // t in [0, 1]
    return min + t * (max - min);
}

// Place an enemy at a random spot inside the arena, kept a little away from the
// walls so cubes never clip into them. Y is half the cube height so it rests
// nicely on the floor (the floor sits at y = 0). Also give it a random heading
// and speed so it drifts around the arena.
static void RespawnEnemy(Enemy *e) {
    float edge = ARENA_HALF - 2.0f;              // padding from the walls
    e->position = (Vector3){
        RandFloat(-edge, edge),
        ENEMY_SIZE / 2.0f,
        RandFloat(-edge, edge)
    };
    float angle = RandFloat(0.0f, 2.0f * PI);    // random compass direction
    float speed = RandFloat(2.5f, 6.0f);         // units per second
    e->velocity = (Vector3){ cosf(angle) * speed, 0.0f, sinf(angle) * speed };
    e->alive = true;
}

// Build a short sound effect from scratch (no audio files needed). We fill a
// buffer of 16-bit samples with a sine wave whose pitch slides from startFreq
// to endFreq, fading out over its length, then hand it to raylib as a Sound.
static Sound GenTone(float startFreq, float endFreq, float seconds) {
    unsigned int sampleRate = 22050;
    unsigned int frames     = (unsigned int)(sampleRate * seconds);
    short *samples = (short *)malloc(frames * sizeof(short));
    for (unsigned int i = 0; i < frames; i++) {
        float t    = (float)i / (float)sampleRate;        // seconds elapsed
        float prog = (float)i / (float)frames;            // 0..1 through the sound
        float freq = startFreq + (endFreq - startFreq) * prog;
        float env  = 1.0f - prog;                          // fade out to avoid a click
        float s    = sinf(2.0f * PI * freq * t) * env * 0.4f;
        samples[i] = (short)(s * 32767.0f);
    }
    Wave wave = { frames, sampleRate, 16, 1, samples };    // 16-bit, mono
    Sound snd = LoadSoundFromWave(wave);                   // raylib copies the data
    free(samples);                                         // ...so we can free ours
    return snd;
}

int main(void) {
    // ---- Window + camera setup --------------------------------------------
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "raylib FPS — ESC to quit");
    InitAudioDevice();    // start the audio system so we can play sound effects

    // Build our two sound effects procedurally (no files to ship):
    //   shoot — a laser "pew" that slides downward in pitch
    //   hit   — a short bright "blip" that rises, for satisfying feedback
    Sound shootSound = GenTone(880.0f, 180.0f, 0.12f);
    Sound hitSound   = GenTone(300.0f, 1200.0f, 0.10f);

    // The first-person camera. raylib's UpdateCamera(CAMERA_FIRST_PERSON) does
    // the mouse-look and WASD math for us.
    Camera camera = { 0 };
    camera.position   = (Vector3){ 0.0f, 2.0f, 6.0f };  // eye height ~2 units
    camera.target     = (Vector3){ 0.0f, 2.0f, 0.0f };  // looking down -Z
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };  // Y is up
    camera.fovy       = 60.0f;                           // vertical field of view
    camera.projection = CAMERA_PERSPECTIVE;

    // Lock the mouse to the window: the cursor is hidden and recentred each
    // frame so you can spin around endlessly. ESC / EnableCursor() reverses it.
    DisableCursor();

    SetTargetFPS(60);                 // Cap the loop at 60 fps
    srand((unsigned int)time(NULL));  // Seed RNG so spawns differ each run

    // ---- Game state -------------------------------------------------------
    Enemy enemies[ENEMY_COUNT];
    for (int i = 0; i < ENEMY_COUNT; i++) RespawnEnemy(&enemies[i]);
    int score = 0;

    // Shot feedback state. When you fire, we record where the shot landed and
    // start a short countdown so we can draw a tracer + marker for a moment.
    float   shotTimer   = 0.0f;          // seconds of feedback left (>0 = show)
    bool    lastShotHit = false;         // did the last shot tag a cube?
    Vector3 shotStart   = { 0 };         // tracer start (near the gun)
    Vector3 shotEnd     = { 0 };         // tracer end (the impact point)

    // ---- Main loop --------------------------------------------------------
    // WindowShouldClose() becomes true when you press ESC or close the window.
    while (!WindowShouldClose()) {

        // ===== 1. UPDATE ===================================================
        UpdateCamera(&camera, CAMERA_FIRST_PERSON);

        float dt = GetFrameTime();   // seconds since last frame

        // Tick down the shot-feedback timer using real elapsed time.
        if (shotTimer > 0.0f) shotTimer -= dt;

        // Move every enemy by its velocity, and bounce it off the arena bounds
        // so the cubes patrol around instead of escaping through the walls.
        float bound = ARENA_HALF - 2.0f;
        for (int i = 0; i < ENEMY_COUNT; i++) {
            enemies[i].position.x += enemies[i].velocity.x * dt;
            enemies[i].position.z += enemies[i].velocity.z * dt;
            if (enemies[i].position.x >  bound) { enemies[i].position.x =  bound; enemies[i].velocity.x *= -1.0f; }
            if (enemies[i].position.x < -bound) { enemies[i].position.x = -bound; enemies[i].velocity.x *= -1.0f; }
            if (enemies[i].position.z >  bound) { enemies[i].position.z =  bound; enemies[i].velocity.z *= -1.0f; }
            if (enemies[i].position.z < -bound) { enemies[i].position.z = -bound; enemies[i].velocity.z *= -1.0f; }
        }

        // Fire on a fresh left-click OR a fresh spacebar press. "Pressed"
        // (not "Down") means one shot per press instead of continuous fire.
        bool fired = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                     IsKeyPressed(KEY_SPACE);

        if (fired) {
            PlaySound(shootSound);   // "pew" on every shot
            shotTimer   = 0.12f;     // show tracer/marker for 0.12s
            lastShotHit = false;
            // Build a ray straight out of the camera through screen centre.
            // GetScreenToWorldRay maps a 2D screen point to a 3D ray; the exact
            // centre of the screen is where our crosshair is drawn.
            Ray shot = GetScreenToWorldRay(
                (Vector2){ SCREEN_WIDTH / 2.0f, SCREEN_HEIGHT / 2.0f },
                camera);

            // Find the CLOSEST enemy the ray hits (so we can't shoot through a
            // near cube to tag one behind it).
            int   bestHit     = -1;
            float bestDistance = 1e30f;

            for (int i = 0; i < ENEMY_COUNT; i++) {
                if (!enemies[i].alive) continue;

                // Axis-aligned bounding box around the cube for ray testing.
                float h = ENEMY_SIZE / 2.0f;
                BoundingBox box = {
                    (Vector3){ enemies[i].position.x - h,
                               enemies[i].position.y - h,
                               enemies[i].position.z - h },
                    (Vector3){ enemies[i].position.x + h,
                               enemies[i].position.y + h,
                               enemies[i].position.z + h }
                };

                RayCollision hit = GetRayCollisionBox(shot, box);
                if (hit.hit && hit.distance < bestDistance) {
                    bestDistance = hit.distance;
                    bestHit      = i;
                }
            }

            // Tracer starts just below the eye (so the line looks like it comes
            // from a gun, not from inside your head).
            shotStart = Vector3Add(camera.position, (Vector3){ 0.0f, -0.3f, 0.0f });

            // Hit something? Score up, respawn it, and end the tracer at the cube.
            if (bestHit >= 0) {
                PlaySound(hitSound);     // satisfying "blip" on a hit
                lastShotHit = true;
                shotEnd = Vector3Add(shot.position,
                                     Vector3Scale(shot.direction, bestDistance));
                score++;
                RespawnEnemy(&enemies[bestHit]);
            } else {
                // Missed: run the tracer far off into the distance.
                shotEnd = Vector3Add(shot.position,
                                     Vector3Scale(shot.direction, 60.0f));
            }
        }

        // ===== 2. DRAW =====================================================
        BeginDrawing();
            ClearBackground(RAYWHITE);

            // ---- 3D world ----
            BeginMode3D(camera);

                // Floor: a flat plane centred at the origin.
                DrawPlane((Vector3){ 0.0f, 0.0f, 0.0f },
                          (Vector2){ ARENA_HALF * 2.0f, ARENA_HALF * 2.0f },
                          DARKGREEN);

                // Four walls around the arena. Each is a long thin box. We place
                // them just inside/at the arena edge, centred at half height.
                float span = ARENA_HALF * 2.0f;
                float y    = WALL_HEIGHT / 2.0f;
                // North (-Z) and South (+Z) walls run along X.
                DrawCube((Vector3){ 0, y, -ARENA_HALF }, span, WALL_HEIGHT, WALL_THICK, DARKGRAY);
                DrawCube((Vector3){ 0, y,  ARENA_HALF }, span, WALL_HEIGHT, WALL_THICK, DARKGRAY);
                // East (+X) and West (-X) walls run along Z.
                DrawCube((Vector3){  ARENA_HALF, y, 0 }, WALL_THICK, WALL_HEIGHT, span, DARKGRAY);
                DrawCube((Vector3){ -ARENA_HALF, y, 0 }, WALL_THICK, WALL_HEIGHT, span, DARKGRAY);

                // Enemy cubes: a bright red body with a gold wireframe, plus a
                // tall yellow "beacon" line rising out of each one so you can
                // spot them across the arena and know which way to turn.
                for (int i = 0; i < ENEMY_COUNT; i++) {
                    if (!enemies[i].alive) continue;
                    DrawCube(enemies[i].position,
                             ENEMY_SIZE, ENEMY_SIZE, ENEMY_SIZE, RED);
                    DrawCubeWires(enemies[i].position,
                                  ENEMY_SIZE, ENEMY_SIZE, ENEMY_SIZE, GOLD);
                    Vector3 top = enemies[i].position;
                    top.y += ENEMY_SIZE / 2.0f;
                    Vector3 sky = top; sky.y += 5.0f;
                    DrawLine3D(top, sky, YELLOW);   // beacon
                }

                // Shot tracer: a bright line from the gun to the impact point,
                // shown briefly after every shot so firing is always visible.
                if (shotTimer > 0.0f) {
                    DrawLine3D(shotStart, shotEnd, lastShotHit ? GREEN : ORANGE);
                }

                // A faint grid on the floor for a sense of depth/movement.
                DrawGrid(40, 1.0f);

            EndMode3D();

            // ---- 2D HUD (drawn on top of the 3D scene) ----

            // Crosshair: a big, thick cross at the exact screen centre.
            // DrawLineEx takes start/end points plus a thickness (in pixels).
            // It flashes green for a moment when your last shot hit a cube, so
            // you get a clear "hit marker" — whether you fired with mouse or space.
            float cx = SCREEN_WIDTH / 2.0f;
            float cy = SCREEN_HEIGHT / 2.0f;
            float arm   = 22.0f;   // how far each line reaches from the centre
            float thick = 4.0f;    // line thickness in pixels
            Color cross = (shotTimer > 0.0f && lastShotHit) ? GREEN : BLACK;
            DrawLineEx((Vector2){ cx - arm, cy }, (Vector2){ cx + arm, cy }, thick, cross); // horizontal
            DrawLineEx((Vector2){ cx, cy - arm }, (Vector2){ cx, cy + arm }, thick, cross); // vertical
            DrawCircleV((Vector2){ cx, cy }, 2.5f, cross);   // centre dot
            if (shotTimer > 0.0f && lastShotHit)
                DrawText("HIT!", (int)cx + 28, (int)cy - 12, 24, GREEN);

            // Score + FPS readouts.
            DrawText(TextFormat("Score: %d", score), 20, 20, 30, BLACK);
            DrawText(TextFormat("FPS: %d", GetFPS()),  20, 56, 20, DARKGRAY);

            // A little control reminder along the bottom.
            DrawText("WASD move   Mouse look   L-Click / Space shoot   ESC quit",
                     20, SCREEN_HEIGHT - 30, 20, GRAY);

        EndDrawing();
    }

    // ---- Cleanup ----------------------------------------------------------
    UnloadSound(shootSound);   // free the generated sound buffers
    UnloadSound(hitSound);
    CloseAudioDevice();        // shut down the audio system
    EnableCursor();            // Give the cursor back before the window closes.
    CloseWindow();             // Close the window and free raylib's resources.
    return 0;
}

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

// An enemy is just a position in the world plus a "is it alive" flag.
// (With respawn-on-hit, alive stays true; the flag is here so the design is
//  easy to extend later, e.g. temporary death animations.)
typedef struct Enemy {
    Vector3 position;
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
// nicely on the floor (the floor sits at y = 0).
static void RespawnEnemy(Enemy *e) {
    float edge = ARENA_HALF - 2.0f;              // padding from the walls
    e->position = (Vector3){
        RandFloat(-edge, edge),
        ENEMY_SIZE / 2.0f,
        RandFloat(-edge, edge)
    };
    e->alive = true;
}

int main(void) {
    // ---- Window + camera setup --------------------------------------------
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "raylib FPS — ESC to quit");

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

        // Tick down the shot-feedback timer using real elapsed time.
        if (shotTimer > 0.0f) shotTimer -= GetFrameTime();

        // Fire on a fresh left-click OR a fresh spacebar press. "Pressed"
        // (not "Down") means one shot per press instead of continuous fire.
        bool fired = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                     IsKeyPressed(KEY_SPACE);

        if (fired) {
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
    EnableCursor();   // Give the cursor back before the window closes.
    CloseWindow();    // Close the window and free raylib's resources.
    return 0;
}

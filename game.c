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
#define ENEMY_COUNT    8       // How many target beers exist at once
#define ENEMY_SIZE     2.0f    // Billboard size / hit-box edge length

#define DEMO_FPS       30      // frame rate for the "--record <dir>" demo video
#define DEMO_SECONDS   10      // length of the recorded demo

// ---- Shotgun viewmodel placement (tuned by eye) ---------------------------
// The shotgun is a side-view sprite drawn in the lower-right corner, flipped so
// the muzzle points up-and-left toward the crosshair. These control where it
// sits, how big it is, and how far it pivots. ANCHOR is the on-screen point the
// gun pivots around (roughly the shooter's grip); ORIGIN is that same point in
// the sprite's own pixel space.
#define GUN_W          720.0f  // on-screen width of the shotgun sprite (px)
#define GUN_ROT        (10.0f) // rotation in degrees (positive tilts muzzle up)
#define GUN_ANCHOR_X   1000.0f // where the grip sits on screen, X
#define GUN_ANCHOR_Y   600.0f  // where the grip sits on screen, Y
#define GUN_ORIGIN_FX  0.60f   // pivot (grip) position within the sprite, as a
#define GUN_ORIGIN_FY  0.58f   //   fraction of width/height (0..1)
// Where the muzzle tip lives in the flipped sprite, as fractions of its
// width/height (measured from the PNG: front of barrel, left edge after flip).
// The flash is placed here via the same transform raylib uses to draw the gun.
#define GUN_MUZZLE_FX  0.00f
#define GUN_MUZZLE_FY  0.1207f

// ---- Game feel -------------------------------------------------------------
#define FIRE_COOLDOWN  0.40f   // min seconds between shots (pump-action cadence)
#define SHELLS_MAX     6       // shells per load before a reload is needed
#define RELOAD_TIME    0.90f   // seconds to reload (gun dips out of view)
#define MAX_PARTICLES  96      // pool size for the beer-splash hit burst

// ---- Score attack ----------------------------------------------------------
#define ROUND_SECONDS  60.0f   // length of one score-attack round
#define BONUS_CHANCE   0.22f   // odds a respawned beer comes back as a bonus beer
#define BONUS_POINTS   3       // what a bonus beer is worth (normal beers = 1)
#define BONUS_SIZE     1.25f   // bonus beers are smaller (harder to hit)...

// An enemy is a position, a velocity (so it can wander), and an alive flag.
// Bonus beers are smaller, faster, and worth more points.
typedef struct Enemy {
    Vector3 position;
    Vector3 velocity;   // units per second; only X and Z are used (cubes slide on the floor)
    float   size;       // billboard size / hit-box edge length
    bool    bonus;      // true = small fast beer worth BONUS_POINTS
    bool    alive;
} Enemy;

// A short-lived bit of "beer splash" flung out when a shot connects.
typedef struct Particle {
    Vector3 position;
    Vector3 velocity;
    float    life;      // seconds of life remaining (<=0 means free/dead)
    float    maxLife;   // initial life, for fading
} Particle;

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
    e->bonus = RandFloat(0.0f, 1.0f) < BONUS_CHANCE;
    e->size  = e->bonus ? BONUS_SIZE : ENEMY_SIZE;
    e->position = (Vector3){
        RandFloat(-edge, edge),
        e->size / 2.0f,
        RandFloat(-edge, edge)
    };
    float angle = RandFloat(0.0f, 2.0f * PI);    // random compass direction
    float speed = e->bonus ? RandFloat(6.0f, 9.5f)    // ...and quicker on their feet
                           : RandFloat(2.5f, 6.0f);   // units per second
    e->velocity = (Vector3){ cosf(angle) * speed, 0.0f, sinf(angle) * speed };
    e->alive = true;
}

// Fling a little burst of beer-splash particles out of an impact point. Reuses
// dead slots from the fixed pool (no allocation), so it's safe to call freely.
static void SpawnBurst(Particle *ps, int max, Vector3 at) {
    int spawned = 0;
    for (int i = 0; i < max && spawned < 14; i++) {
        if (ps[i].life > 0.0f) continue;            // slot still in use
        float ang = RandFloat(0.0f, 2.0f * PI);
        float out = RandFloat(2.0f, 7.0f);          // outward speed
        ps[i].position = at;
        ps[i].velocity = (Vector3){ cosf(ang) * out,
                                    RandFloat(2.5f, 6.5f),   // pop upward
                                    sinf(ang) * out };
        ps[i].maxLife = ps[i].life = RandFloat(0.25f, 0.5f);
        spawned++;
    }
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

// Mix one GenTone-style sweep into a 16-bit mono buffer starting at sample
// `start`, summing (and clamping) so overlapping sounds add together. Used by
// the --record demo to synthesise a soundtrack that matches the on-screen shots.
static void MixTone(short *buf, long len, long start,
                    float startFreq, float endFreq, float seconds, int sampleRate) {
    long n = (long)(sampleRate * seconds);
    for (long i = 0; i < n; i++) {
        long idx = start + i;
        if (idx < 0 || idx >= len) continue;
        float t    = (float)i / (float)sampleRate;
        float prog = (float)i / (float)n;
        float freq = startFreq + (endFreq - startFreq) * prog;
        float env  = 1.0f - prog;
        float s    = sinf(2.0f * PI * freq * t) * env * 0.4f;
        int   v    = buf[idx] + (int)(s * 32767.0f);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        buf[idx] = (short)v;
    }
}

// Unit forward vector for a yaw (around Y, 0 = looking down -Z) and pitch.
static Vector3 ForwardFromYawPitch(float yaw, float pitch) {
    return (Vector3){ sinf(yaw) * cosf(pitch),
                      sinf(pitch),
                     -cosf(yaw) * cosf(pitch) };
}

int main(int argc, char **argv) {
    // Optional self-test: "./game --shot out.png" renders a few frames with a
    // beer parked in front and the gun firing, saves a screenshot, then exits.
    // Used during development to tune the viewmodel without grabbing the mouse.
    // And "./game --record <dir>" plays a scripted auto-aim demo, writing one
    // PNG per frame plus a synthesised audio.wav into <dir>, then exits — the
    // raw material for the demo video (see make-video.sh).
    const char *shotPath = NULL;
    const char *recPath  = NULL;
    for (int i = 1; i < argc - 1; i++) {
        if (TextIsEqual(argv[i], "--shot"))   shotPath = argv[i + 1];
        if (TextIsEqual(argv[i], "--record")) recPath  = argv[i + 1];
    }

    // ---- Window + camera setup --------------------------------------------
    if (recPath) SetTraceLogLevel(LOG_WARNING);   // quiet logs while recording
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "raylib FPS — ESC to quit");
    InitAudioDevice();    // start the audio system so we can play sound effects

    // ---- Textures ---------------------------------------------------------
    // Load after the window (textures need the GPU context). The beer mug is
    // drawn as a billboard (a sprite that always faces the camera); the shotgun
    // is the 2D viewmodel. Bilinear filtering keeps both smooth when scaled.
    Texture2D beerTex    = LoadTexture("assets/beer.png");
    Texture2D shotgunTex = LoadTexture("assets/shotgun.png");
    SetTextureFilter(beerTex,    TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(shotgunTex, TEXTURE_FILTER_BILINEAR);

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
    // (Skip this in screenshot mode so the test run doesn't grab the cursor.)
    if (!shotPath && !recPath) DisableCursor();

    SetTargetFPS(60);                 // Cap the loop at 60 fps
    srand((unsigned int)time(NULL));  // Seed RNG so spawns differ each run

    // ---- Game state -------------------------------------------------------
    Enemy enemies[ENEMY_COUNT];
    for (int i = 0; i < ENEMY_COUNT; i++) RespawnEnemy(&enemies[i]);
    int score = 0;

    // Score-attack state: a round lasts ROUND_SECONDS; when time runs out the
    // game freezes on a "TIME'S UP" screen until R restarts it. `best` tracks
    // the highest round score this session. `lastPoints` feeds the +N popup.
    float timeLeft   = ROUND_SECONDS;
    bool  gameOver   = false;
    int   best       = 0;
    int   lastPoints = 1;

    // Shot feedback state. When you fire, we record where the shot landed and
    // start a short countdown so we can draw a tracer + marker for a moment.
    float   shotTimer   = 0.0f;          // seconds of feedback left (>0 = show)
    bool    lastShotHit = false;         // did the last shot tag a cube?
    Vector3 shotStart   = { 0 };         // tracer start (near the gun)
    Vector3 shotEnd     = { 0 };         // tracer end (the impact point)
    float   muzzleTimer = 0.0f;          // brief muzzle-flash + recoil countdown

    // Game-feel state: fire-rate gate, shells + reload, screen shake, gun bob,
    // and the particle pool for hit splashes.
    float   fireTimer   = 0.0f;          // time until the next shot is allowed
    int     shells      = SHELLS_MAX;    // loaded shells; 0 forces a reload
    float   reloadTimer = 0.0f;          // >0 while reloading (gun is lowered)
    float   shakeTimer  = 0.0f;          // >0 while the view is shaking
    float   shakeMag    = 0.0f;          // current shake magnitude (world units)
    float   bobPhase    = 0.0f;          // advances with movement for the gun bob
    Vector3 prevCamPos  = camera.position;
    Particle particles[MAX_PARTICLES] = { 0 };

    // Screenshot mode: park a beer right in front of the camera, light up the
    // muzzle flash, and count frames so we can grab one clean frame and quit.
    int shotFrame = 0;
    if (shotPath) {
        enemies[0].bonus = false;            // deterministic screenshot
        enemies[0].size  = ENEMY_SIZE;
        enemies[0].position = (Vector3){ 0.0f, ENEMY_SIZE / 2.0f, 0.0f };
        camera.position = (Vector3){ 0.0f, 2.0f, 7.0f };
        camera.target   = (Vector3){ 0.0f, 1.5f, 0.0f };
        muzzleTimer = 0.06f;   // show the flash
        shotTimer   = 0.12f;
        lastShotHit = true;
        shotStart = Vector3Add(camera.position, (Vector3){ 0.0f, -0.3f, 0.0f });
        shotEnd   = enemies[0].position;
    }

    // Demo-recording state. We drive the camera ourselves (yaw/pitch), auto-aim
    // at beers, and accumulate a soundtrack in recTrack as shots fire.
    int   recFrame = 0;
    int   recTotal = DEMO_FPS * DEMO_SECONDS;
    float yaw = 0.0f, pitch = 0.0f;       // manual look angles for record mode
    int   recSampleRate = 22050;
    long  recTrackLen = 0;
    short *recTrack = NULL;
    if (recPath) {
        camera.position = (Vector3){ 0.0f, 2.4f, 7.0f };
        recTrackLen = (long)recSampleRate * (DEMO_SECONDS + 1);  // +1s of tail
        recTrack = (short *)calloc(recTrackLen, sizeof(short));
    }

    // ---- Main loop --------------------------------------------------------
    // WindowShouldClose() becomes true when you press ESC or close the window.
    // While recording we instead loop purely on the frame count, so a stray
    // focus/close event can't cut the demo short before all frames are written.
    while (recPath ? (recFrame < recTotal) : !WindowShouldClose()) {

        // ===== 1. UPDATE ===================================================
        // In screenshot mode we freeze the camera/enemies so the frame is
        // deterministic; otherwise drive the camera from mouse + WASD as usual.
        if (!shotPath && !recPath) UpdateCamera(&camera, CAMERA_FIRST_PERSON);

        // Fixed timestep when recording (deterministic motion regardless of how
        // fast frames are actually captured); real elapsed time otherwise.
        float dt = recPath ? (1.0f / DEMO_FPS) : (shotPath ? 0.0f : GetFrameTime());

        // Tick down all the short timers.
        if (shotTimer   > 0.0f) shotTimer   -= dt;
        if (muzzleTimer > 0.0f) muzzleTimer -= dt;
        if (fireTimer   > 0.0f) fireTimer   -= dt;
        if (shakeTimer  > 0.0f) shakeTimer  -= dt;
        bool wasReloading = (reloadTimer > 0.0f);
        if (reloadTimer > 0.0f) reloadTimer -= dt;
        if (wasReloading && reloadTimer <= 0.0f) shells = SHELLS_MAX;  // reload finished

        // Run the round clock (only during real play — the screenshot and demo
        // modes have no countdown). Hitting zero ends the round.
        if (!shotPath && !recPath && !gameOver) {
            timeLeft -= dt;
            if (timeLeft <= 0.0f) {
                timeLeft = 0.0f;
                gameOver = true;
                if (score > best) best = score;
            }
        }

        // On the game-over screen R restarts a fresh round instead of reloading.
        if (gameOver && IsKeyPressed(KEY_R)) {
            for (int i = 0; i < ENEMY_COUNT; i++) RespawnEnemy(&enemies[i]);
            for (int i = 0; i < MAX_PARTICLES; i++) particles[i].life = 0.0f;
            score = 0; shells = SHELLS_MAX;
            shotTimer = muzzleTimer = fireTimer = reloadTimer = shakeTimer = 0.0f;
            timeLeft = ROUND_SECONDS;
            gameOver = false;
        }

        // Start a reload when empty, or on demand with R (when not already at it).
        bool wantReload = !gameOver && IsKeyPressed(KEY_R) && shells < SHELLS_MAX;
        if (!gameOver && reloadTimer <= 0.0f && (shells <= 0 || wantReload)) reloadTimer = RELOAD_TIME;

        // Gun bob: advance a phase by how fast the camera is moving (plus a slow
        // idle drift), so the viewmodel sways while you walk and breathes at rest.
        float camSpeed = (dt > 0.0f)
            ? Vector3Length(Vector3Subtract(camera.position, prevCamPos)) / dt : 0.0f;
        prevCamPos = camera.position;
        bobPhase += dt * (1.5f + camSpeed * 1.2f);

        // Advance hit particles (simple gravity + fade).
        for (int i = 0; i < MAX_PARTICLES; i++) {
            if (particles[i].life <= 0.0f) continue;
            particles[i].life -= dt;
            particles[i].velocity.y -= 12.0f * dt;          // gravity
            particles[i].position = Vector3Add(particles[i].position,
                                               Vector3Scale(particles[i].velocity, dt));
        }

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

        // Demo auto-pilot: drift the camera for life, smoothly swing the aim
        // onto whichever beer is nearest the current view, and fire once the
        // crosshair is on target (throttled by a cooldown).
        bool autoFire = false;
        if (recPath) {
            float t = recFrame / (float)DEMO_FPS;
            camera.position.x = 4.0f * sinf(t * 0.25f);
            camera.position.y = 2.4f + 0.12f * sinf(t * 1.7f);
            camera.position.z = 7.0f;

            Vector3 fwd = ForwardFromYawPitch(yaw, pitch);
            int tgt = -1; float bestAng = 1e9f; Vector3 tdir = { 0 };
            for (int i = 0; i < ENEMY_COUNT; i++) {
                if (!enemies[i].alive) continue;
                Vector3 d = Vector3Normalize(Vector3Subtract(enemies[i].position, camera.position));
                float a = acosf(Clamp(Vector3DotProduct(fwd, d), -1.0f, 1.0f));
                if (a < bestAng) { bestAng = a; tgt = i; tdir = d; }
            }
            if (tgt >= 0) {
                float desYaw   = atan2f(tdir.x, -tdir.z);
                float desPitch = asinf(Clamp(tdir.y, -1.0f, 1.0f));
                float dy = desYaw - yaw;
                while (dy >  PI) dy -= 2.0f * PI;
                while (dy < -PI) dy += 2.0f * PI;
                yaw   += dy * 0.20f;                       // ease toward the target
                pitch += (desPitch - pitch) * 0.20f;
            }
            Vector3 fwd2 = ForwardFromYawPitch(yaw, pitch);
            camera.target = Vector3Add(camera.position, fwd2);

            if (tgt >= 0) {
                Vector3 d = Vector3Normalize(Vector3Subtract(enemies[tgt].position, camera.position));
                float err = acosf(Clamp(Vector3DotProduct(fwd2, d), -1.0f, 1.0f));
                autoFire = (err < 0.05f);   // aimed; fire-rate + ammo gated below
            }
        }

        // The trigger is held (hold to keep firing) via auto-pilot, the mouse,
        // or space. A shot only happens if the fire-rate cooldown has elapsed,
        // we have a shell, and we're not mid-reload — so it paces like a pump gun.
        bool wantFire = autoFire ||
                        IsMouseButtonDown(MOUSE_BUTTON_LEFT) ||
                        IsKeyDown(KEY_SPACE);
        bool fired = !gameOver && wantFire &&
                     fireTimer <= 0.0f && shells > 0 && reloadTimer <= 0.0f;

        if (fired) {
            fireTimer = FIRE_COOLDOWN;   // throttle to the pump-action cadence
            shells--;                    // spend a shell
            shakeTimer = 0.10f; shakeMag = 0.07f;   // a little kick on every shot
            PlaySound(shootSound);   // "pew" on every shot
            if (recPath)             // ...and bake it into the demo soundtrack
                MixTone(recTrack, recTrackLen,
                        (long)((recFrame / (float)DEMO_FPS) * recSampleRate),
                        880.0f, 180.0f, 0.12f, recSampleRate);
            shotTimer   = 0.12f;     // show tracer/marker for 0.12s
            muzzleTimer = 0.06f;     // flash + recoil for a brief moment
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
                float h = enemies[i].size / 2.0f;
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
                if (recPath)
                    MixTone(recTrack, recTrackLen,
                            (long)((recFrame / (float)DEMO_FPS) * recSampleRate),
                            300.0f, 1200.0f, 0.10f, recSampleRate);
                lastShotHit = true;
                shotEnd = Vector3Add(shot.position,
                                     Vector3Scale(shot.direction, bestDistance));
                lastPoints = enemies[bestHit].bonus ? BONUS_POINTS : 1;
                score += lastPoints;
                shakeTimer = 0.16f; shakeMag = 0.16f;       // bigger jolt on a hit
                SpawnBurst(particles, MAX_PARTICLES, shotEnd);  // beer-splash burst
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
            // Screen shake: jitter a copy of the camera (position + target by the
            // same offset, so we stay pointed the same way) while shakeTimer runs.
            Camera drawCam = camera;
            if (shakeTimer > 0.0f) {
                Vector3 jit = { RandFloat(-shakeMag, shakeMag),
                                RandFloat(-shakeMag, shakeMag),
                                RandFloat(-shakeMag, shakeMag) };
                drawCam.position = Vector3Add(drawCam.position, jit);
                drawCam.target   = Vector3Add(drawCam.target, jit);
            }
            BeginMode3D(drawCam);

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

                // Enemies are beers: each one is a billboard (a flat sprite that
                // always turns to face the camera, classic Doom-style) showing a
                // frothy mug. A short gold beacon rises out of each so you can
                // still spot them wandering across the arena.
                // Bonus beers are smaller, faster, tinted gold, and fly a red
                // beacon — they're worth BONUS_POINTS if you can tag one.
                for (int i = 0; i < ENEMY_COUNT; i++) {
                    if (!enemies[i].alive) continue;
                    DrawBillboard(drawCam, beerTex, enemies[i].position,
                                  enemies[i].size,
                                  enemies[i].bonus ? GOLD : WHITE);
                    Vector3 top = enemies[i].position;
                    top.y += enemies[i].size / 2.0f;
                    Vector3 sky = top; sky.y += 4.0f;
                    DrawLine3D(top, sky, enemies[i].bonus ? RED : GOLD);   // beacon
                }

                // Shot tracer: a bright line from the gun to the impact point,
                // shown briefly after every shot so firing is always visible.
                if (shotTimer > 0.0f) {
                    DrawLine3D(shotStart, shotEnd, lastShotHit ? GREEN : ORANGE);
                }

                // Hit particles: little golden cubes of "beer" that fade as they
                // arc and fall. Drawn after the world so they sit on top.
                for (int i = 0; i < MAX_PARTICLES; i++) {
                    if (particles[i].life <= 0.0f) continue;
                    float f = particles[i].life / particles[i].maxLife;   // 1..0
                    DrawCube(particles[i].position, 0.18f, 0.18f, 0.18f,
                             Fade(GOLD, f));
                }

                // A faint grid on the floor for a sense of depth/movement.
                DrawGrid(40, 1.0f);

            EndMode3D();

            // ---- Gun viewmodel (2D, drawn over the 3D scene) ----
            // The shotgun sprite, held in the lower-right and pivoted so the
            // muzzle points up toward the crosshair. When you fire it kicks back
            // (recoil slides it down briefly) and a flash blooms at the barrel.
            float recoil = (muzzleTimer > 0.0f) ? 26.0f * (muzzleTimer / 0.06f) : 0.0f;

            float gunW = GUN_W;
            float gunH = gunW * (float)shotgunTex.height / (float)shotgunTex.width;

            // Source rect with a NEGATIVE width flips the sprite horizontally so
            // the muzzle ends up on the left (pointing toward screen centre).
            Rectangle gunSrc = { 0.0f, 0.0f,
                                 -(float)shotgunTex.width, (float)shotgunTex.height };
            // Offsets layered onto the grip anchor: recoil (kick down on a shot),
            // a reload dip (gun swings out of view and back), and a gentle bob
            // that sways with movement / breathes at rest.
            float dipY = (reloadTimer > 0.0f)
                       ? sinf((1.0f - reloadTimer / RELOAD_TIME) * PI) * 220.0f : 0.0f;
            float bobX = sinf(bobPhase) * 7.0f;
            float bobY = sinf(bobPhase * 2.0f) * 5.0f;
            float ax = GUN_ANCHOR_X + bobX;
            float ay = GUN_ANCHOR_Y + recoil + dipY + bobY;

            // We pivot around the grip: ORIGIN is that point in sprite space, and
            // we place it at the (offset) anchor on screen.
            Vector2 gunOrigin = { gunW * GUN_ORIGIN_FX, gunH * GUN_ORIGIN_FY };
            Rectangle gunDst  = { ax, ay, gunW, gunH };
            DrawTexturePro(shotgunTex, gunSrc, gunDst, gunOrigin, GUN_ROT, WHITE);

            // Place the muzzle flash at the barrel tip by running the muzzle's
            // sprite-space point through the SAME pivot/rotation raylib used to
            // draw the gun (matching DrawTexturePro: rotation about the origin).
            float rad = GUN_ROT * DEG2RAD, cr = cosf(rad), sr = sinf(rad);
            float mlx = gunW * GUN_MUZZLE_FX - gunOrigin.x;
            float mly = gunH * GUN_MUZZLE_FY - gunOrigin.y;
            Vector2 muzzle = { ax + mlx * cr - mly * sr,
                               ay + mlx * sr + mly * cr };

            // Muzzle flash: a quick burst of bright shapes right at the tip.
            if (muzzleTimer > 0.0f) {
                DrawCircleV(muzzle, 34.0f, Fade(ORANGE, 0.85f));
                DrawCircleV(muzzle, 20.0f, Fade(YELLOW, 0.95f));
                DrawCircleV(muzzle, 9.0f, WHITE);
                // Four little spikes for a star-burst look.
                DrawLineEx(muzzle, (Vector2){ muzzle.x - 52, muzzle.y }, 6.0f, YELLOW);
                DrawLineEx(muzzle, (Vector2){ muzzle.x + 52, muzzle.y }, 6.0f, YELLOW);
                DrawLineEx(muzzle, (Vector2){ muzzle.x, muzzle.y - 52 }, 6.0f, YELLOW);
                DrawLineEx(muzzle, (Vector2){ muzzle.x, muzzle.y + 52 }, 6.0f, YELLOW);
            }

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
                DrawText(TextFormat("+%d", lastPoints), (int)cx + 28, (int)cy - 12, 24,
                         lastPoints > 1 ? GOLD : GREEN);

            // Score readout (top-left); session best lives under the FPS counter.
            DrawText(TextFormat("Score: %d", score), 20, 20, 30, BLACK);
            if (best > 0)
                DrawText(TextFormat("Best: %d", best), SCREEN_WIDTH - 110, 50, 20, DARKGRAY);

            // Round timer (top-centre); turns red for the last 10 seconds.
            if (!shotPath && !recPath) {
                int secs = (int)ceilf(timeLeft);
                const char *clock = TextFormat("%d:%02d", secs / 60, secs % 60);
                int cw2 = MeasureText(clock, 40);
                DrawText(clock, SCREEN_WIDTH / 2 - cw2 / 2, 16, 40,
                         (timeLeft <= 10.0f && !gameOver) ? RED : BLACK);
            }

            // Shell gauge: one shotgun-shell pip per shell, filled = loaded.
            for (int s = 0; s < SHELLS_MAX; s++) {
                Rectangle pip = { 20.0f + s * 24.0f, 60.0f, 18.0f, 28.0f };
                bool loaded = (s < shells);
                DrawRectangleRec(pip, loaded ? (Color){ 190, 40, 40, 255 } : Fade(DARKGRAY, 0.4f));
                DrawRectangle((int)pip.x, (int)(pip.y + pip.height - 9), (int)pip.width, 9,
                              loaded ? GOLD : Fade(GRAY, 0.5f));               // brass base
                DrawRectangleLinesEx(pip, 2.0f, BLACK);
            }
            if (reloadTimer > 0.0f)
                DrawText("RELOADING", 20, 94, 22, MAROON);

            // Game over: dim the world and show the round result until R.
            if (gameOver) {
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.55f));
                const char *go = "TIME'S UP!";
                int gfs = 70, gw = MeasureText(go, gfs);
                DrawText(go, SCREEN_WIDTH / 2 - gw / 2, SCREEN_HEIGHT / 2 - 120, gfs, RAYWHITE);
                const char *sc = TextFormat("Score: %d    Best: %d", score, best);
                int sfs2 = 40, sw2 = MeasureText(sc, sfs2);
                DrawText(sc, SCREEN_WIDTH / 2 - sw2 / 2, SCREEN_HEIGHT / 2 - 20, sfs2, GOLD);
                const char *rr = "Press R to restart";
                int rfs = 30, rw = MeasureText(rr, rfs);
                DrawText(rr, SCREEN_WIDTH / 2 - rw / 2, SCREEN_HEIGHT / 2 + 50, rfs, RAYWHITE);
            }

            if (!recPath) {
                // Normal play: FPS (top-right) + a control reminder along the bottom.
                DrawText(TextFormat("FPS: %d", GetFPS()), SCREEN_WIDTH - 110, 20, 20, DARKGRAY);
                DrawText("WASD move   Mouse look   Hold L-Click / Space fire   R reload   ESC quit",
                         20, SCREEN_HEIGHT - 30, 20, GRAY);
            } else {
                // Recording: a caption bar for sharing, plus a brief intro title.
                const char *cap = "Native FPS in C + raylib  -  built with Claude Code";
                int cfs = 26, cw = MeasureText(cap, cfs);
                DrawRectangle(0, SCREEN_HEIGHT - 50, SCREEN_WIDTH, 50, Fade(BLACK, 0.5f));
                DrawText(cap, SCREEN_WIDTH / 2 - cw / 2, SCREEN_HEIGHT - 40, cfs, RAYWHITE);

                float t = recFrame / (float)DEMO_FPS;
                if (t < 1.8f) {                                   // fade out over the last 0.6s
                    float a = (t < 1.2f) ? 1.0f : (1.8f - t) / 0.6f;
                    DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Fade(BLACK, 0.35f * a));
                    const char *tt = "Built with Claude Code";
                    int tfs = 70, tw = MeasureText(tt, tfs);
                    DrawText(tt, SCREEN_WIDTH / 2 - tw / 2, SCREEN_HEIGHT / 2 - 90, tfs, Fade(RAYWHITE, a));
                    const char *st = "a native raylib shotgun game, in C";
                    int sfs = 30, sw = MeasureText(st, sfs);
                    DrawText(st, SCREEN_WIDTH / 2 - sw / 2, SCREEN_HEIGHT / 2 + 0, sfs, Fade(GOLD, a));
                }
            }

        EndDrawing();

        // Screenshot mode: let a few frames settle (so textures are uploaded
        // and the flash is showing), grab one, then quit.
        if (shotPath) {
            muzzleTimer = 0.06f;            // keep the flash lit every frame
            if (++shotFrame >= 3) { TakeScreenshot(shotPath); break; }
        }

        // Record mode: save this frame, then when the demo is over write the
        // accumulated soundtrack as a WAV and stop. (We use LoadImageFromScreen
        // + ExportImage rather than TakeScreenshot, which rewrites the path
        // relative to the working dir and so can't target an arbitrary folder.)
        if (recPath) {
            Image fb = LoadImageFromScreen();
            ExportImage(fb, TextFormat("%s/frame_%05d.png", recPath, recFrame));
            UnloadImage(fb);
            if (++recFrame >= recTotal) {
                Wave w = { (unsigned int)recTrackLen, (unsigned int)recSampleRate,
                           16, 1, recTrack };
                ExportWave(w, TextFormat("%s/audio.wav", recPath));
                break;
            }
        }
    }

    // ---- Cleanup ----------------------------------------------------------
    if (recTrack) free(recTrack);  // free the demo soundtrack buffer
    UnloadTexture(beerTex);        // free the GPU textures
    UnloadTexture(shotgunTex);
    UnloadSound(shootSound);   // free the generated sound buffers
    UnloadSound(hitSound);
    CloseAudioDevice();        // shut down the audio system
    EnableCursor();            // Give the cursor back before the window closes.
    CloseWindow();             // Close the window and free raylib's resources.
    return 0;
}

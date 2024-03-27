#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

constexpr int screenWidth = 800;
constexpr int screenHeight = 450;

constexpr int shadowMapResolution = 1024;

RenderTexture2D LoadShadowMapRenderTexture(int width, int height);
void UnloadShadowMapRenderTexture(RenderTexture2D target);
void DrawScene(Model cube);

int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Percentage-Closer Soft Shadows");
    SetTargetFPS(60);

    Camera3D cam = Camera3D{ 0 };
    cam.position = Vector3{ 9.5f, 7.5f, -6.0f };
    cam.target = Vector3Zero();
    cam.projection = CAMERA_PERSPECTIVE;
    cam.up = Vector3{ 0.0f, 1.0f, 0.0f };
    cam.fovy = 45.0f;

    Shader shadowShader = LoadShader("resources/shaders/shadowMap.vert", "resources/shaders/shadowMap.frag");
    shadowShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shadowShader, "viewPos");
    Vector3 lightDir = Vector3Normalize(Vector3{ 0.0f, -1.0f, -1.0f });
    Color lightColor = WHITE;
    Vector4 lightColorNormalized = ColorNormalize(lightColor);
    int lightDirLoc = GetShaderLocation(shadowShader, "lightDir");
    int lightColLoc = GetShaderLocation(shadowShader, "lightColor");
    SetShaderValue(shadowShader, lightDirLoc, &lightDir, SHADER_UNIFORM_VEC3);
    SetShaderValue(shadowShader, lightColLoc, &lightColorNormalized, SHADER_UNIFORM_VEC4);
    int ambientLoc = GetShaderLocation(shadowShader, "ambient");
    float ambient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SetShaderValue(shadowShader, ambientLoc, ambient, SHADER_UNIFORM_VEC4);
    int lightVPLoc = GetShaderLocation(shadowShader, "lightVP");
    int shadowMapLoc = GetShaderLocation(shadowShader, "shadowMap");
    SetShaderValue(shadowShader, GetShaderLocation(shadowShader, "shadowMapResolution"), &shadowMapResolution, SHADER_UNIFORM_INT);
    int frustumWidthLoc = GetShaderLocation(shadowShader, "frustumWidth");

    Model cube = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    cube.materials[0].shader = shadowShader;

    RenderTexture2D shadowMap = LoadShadowMapRenderTexture(shadowMapResolution, shadowMapResolution);
    // For the shadow mapping algorithm, we will be rendering everything from the light's point of view
    Camera3D lightCam = Camera3D{ 0 };
    lightCam.position = Vector3Scale(lightDir, -8.0f);
    lightCam.target = Vector3Zero();
    // Use an orthographic projection for directional lights
    lightCam.projection = CAMERA_ORTHOGRAPHIC;
    lightCam.up = Vector3{ 0.0f, 1.0f, 0.0f };
    lightCam.fovy = 20.0f;

    HideCursor();

    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        float dt = GetFrameTime();

        Vector3 cameraPos = cam.position;
        SetShaderValue(shadowShader, shadowShader.locs[SHADER_LOC_VECTOR_VIEW], &cameraPos, SHADER_UNIFORM_VEC3);
        UpdateCamera(&cam, CAMERA_FREE);
        SetMousePosition(screenWidth / 2, screenHeight / 2);

        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();

        // First, render all objects into the shadow map
        // The idea is, we record all the objects' depths (as rendered from the light source's point of view) in a buffer
        // Anything that is "visible" to the light is in light, anything that isn't is in shadow
        // We can later use the depth buffer when rendering everything from the player's point of view
        // to determine whether a given point is "visible" to the light

        // Record the light matrices for future use!
        Matrix lightView;
        Matrix lightProj;
        BeginTextureMode(shadowMap);
        ClearBackground(WHITE);
        BeginMode3D(lightCam);
        lightView = rlGetMatrixModelview();
        lightProj = rlGetMatrixProjection();
        DrawScene(cube);
        EndMode3D();
        EndTextureMode();
        Matrix lightViewProj = MatrixMultiply(lightView, lightProj);

        ClearBackground(RAYWHITE);

        SetShaderValueMatrix(shadowShader, lightVPLoc, lightViewProj);

        rlEnableShader(shadowShader.id);
        int slot = 10; // Can be anything 0 to 15, but 0 will probably be taken up
        rlActiveTextureSlot(10);
        rlEnableTexture(shadowMap.depth.id);
        rlSetUniform(shadowMapLoc, &slot, SHADER_UNIFORM_INT, 1);
        float frustumRight = cam.fovy / 2.0f; // Note: if the shadow map were to have different dimensions along the x and y axes, this would have to be multiplied by the aspect ratio (x / y)
        rlSetUniform(frustumWidthLoc, &frustumRight, SHADER_UNIFORM_FLOAT, 1);

        BeginMode3D(cam);

        // Draw the same exact things as we drew in the shadow map!
        DrawScene(cube);

        EndMode3D();

        DrawText(TextFormat("Frame time: %f ms", dt * 1000.0f), 20, 20, 20, BLUE);

        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------

    UnloadShader(shadowShader);
    UnloadModel(cube);
    UnloadShadowMapRenderTexture(shadowMap);

    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    return 0;
}

RenderTexture2D LoadShadowMapRenderTexture(int width, int height)
{
    RenderTexture2D target = { 0 };

    target.id = rlLoadFramebuffer(); // Load an empty framebuffer
    target.texture.width = width;
    target.texture.height = height;

    if (target.id > 0)
    {
        rlEnableFramebuffer(target.id);

        // Create depth texture
        // We don't need a color texture for the shadow map
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19;       //DEPTH_COMPONENT_24BIT?
        target.depth.mipmaps = 1;

        // Attach depth texture to FBO
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if fbo is complete with attachments (valid)
        if (rlFramebufferComplete(target.id)) TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);

        rlDisableFramebuffer();
    }
    else TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");

    return target;
}

// Unload shadow map render texture from GPU memory (VRAM)
void UnloadShadowMapRenderTexture(RenderTexture2D target)
{
    if (target.id > 0)
    {
        // NOTE: Depth texture/renderbuffer is automatically
        // queried and deleted before deleting framebuffer
        rlUnloadFramebuffer(target.id);
    }
}

void DrawScene(Model cube)
{
    DrawModelEx(cube, Vector3Zero(), Vector3 { 0.0f, 1.0f, 0.0f }, 0.0f, Vector3 { 10.0f, 1.0f, 10.0f }, BLUE);
    DrawModelEx(cube, Vector3{ 0.0f, 1.5f, 4.9f }, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{10.0f, 2.0f, 0.2f}, WHITE);

    DrawModelEx(cube, Vector3{ 3.0f, 3.5f, 4.9f }, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{4.0f, 2.0f, 0.2f}, WHITE);
    DrawModelEx(cube, Vector3{ -3.0f, 3.5f, 4.9f }, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{4.0f, 2.0f, 0.2f}, WHITE);

    DrawModelEx(cube, Vector3{ 0.0f, 5.5f, 4.9f }, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{10.0f, 2.0f, 0.2f}, WHITE);
}

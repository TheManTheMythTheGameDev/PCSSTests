#version 330

// This shader is based on the basic lighting shader
// This only supports one light, which is directional, and it (of course) supports shadows

// Input vertex attributes (from vertex shader)
in vec3 fragPosition;
in vec2 fragTexCoord;
//in vec4 fragColor;
in vec3 fragNormal;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// Input lighting values
uniform vec3 lightDir;
uniform vec4 lightColor;
uniform vec4 ambient;
uniform vec3 viewPos;

// Input shadowmapping values
uniform mat4 lightVP; // Light source view-projection matrix
uniform sampler2D shadowMap;

uniform int shadowMapResolution;
uniform float frustumWidth;

// This dictates the blurriness of the shadows: higher light size, blurrier shadows
#define LIGHT_SIZE 100

#define BLOCKER_SEARCH_WIDTH 128
#define PCF_WIDTH 8
#define BLOCKER_SEARCH_NUM_SAMPLES 16
#define PCF_NUM_SAMPLES 32

vec2 RotateAroundOrigin(vec2 point, float angle)
{
    float cosA = cos(angle);
    float sinA = sin(angle);
    float x = point.x;
    float y = point.y;
    return vec2(x * cosA - y * sinA, x * sinA + y * cosA);
}

float InterleavedGradientNoise(vec2 pos)
{
    const vec3 magic = vec3(0.06711056f, 0.00583715f, 52.9829189f);
	return fract(magic.z * fract(dot(pos, magic.xy)));
}

vec2 RandomRotate(vec2 point)
{
    float randomAngle = InterleavedGradientNoise(gl_FragCoord.xy) * 360.0;
    randomAngle = radians(randomAngle);

    vec2 result = RotateAroundOrigin(point, randomAngle);

    return result;
}

vec2 VogelDiskSample(int sampleIndex, int sampleCount, float phi)
{
    float goldenAngle = 2.4f;

    float r = sqrt(sampleIndex + 0.5f) / sqrt(sampleCount);
    float theta = sampleIndex * goldenAngle + phi;

    float sinT = sin(theta);
    float cosT = cos(theta);

    return vec2(cosT, sinT) * r;
}

ivec2 GetSampleOffset(int i, int maxI, float width)
{
    return ivec2(RandomRotate(VogelDiskSample(i, maxI, InterleavedGradientNoise(gl_FragCoord.xy))) * width);
}

float GetShadow(vec3 normal, vec3 l)
{
    // Shadow calculations
    vec4 fragPosLightSpace = lightVP * vec4(fragPosition, 1);
    fragPosLightSpace.xyz /= fragPosLightSpace.w; // Perform the perspective division
    fragPosLightSpace.xyz = (fragPosLightSpace.xyz + 1.0f) / 2.0f; // Transform from [-1, 1] range to [0, 1] range
    vec2 sampleCoords = fragPosLightSpace.xy;
    float curDepth = fragPosLightSpace.z;
    // Slope-scale depth bias: depth biasing reduces "shadow acne" artifacts, where dark stripes appear all over the scene.
    // The solution is adding a small bias to the depth
    // In this case, the bias is proportional to the slope of the surface, relative to the light
    float bias = max(0.0002 * (1.0 - dot(normal, l)), 0.00002) + 0.00001;
    int shadowCounter = 0;
    const int numSamples = 9;
    float shadow = 0.0;
    // PCSS (percentage-closer soft shadows) algorithm:
    // For PCF (percentage-closer filtering): Instead of testing if just one point
    // is closer to the current point, we test the surrounding points as well.
    // This creates a blurring effect, softening the shadow and hiding aliasing.
    // PCSS extends this by taking into account the average shadow-caster depth.
    // Assuming the light source, the shadow caster, and the shadow receiver
    // are three parallel planes, we can adapt the PCF search size to achieve
    // a plausible contact-hardening shadow effect.

    // PCF:
    /*vec2 texelSize = vec2(1.0f / float(shadowMapResolution));
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float sampleDepth = texture(shadowMap, sampleCoords + texelSize * vec2(x, y)).r;
            if (curDepth - bias > sampleDepth)
            {
                shadowCounter++;
            }
        }
    }
    shadow = float(shadowCounter) / float(numSamples);*/

    // PCSS:
    // Some algorithms from here: https://developer.download.nvidia.com/whitepapers/2008/PCSS_Integration.pdf
    float lightSizeUV = LIGHT_SIZE / frustumWidth;

    vec2 texelSize = vec2(1.0 / float(shadowMapResolution));

    float blockerAvg = 0.0f;
    int numBlockers = 0;
    int blockerSearchWidth = int(lightSizeUV * curDepth);
    for (int i = 0; i < BLOCKER_SEARCH_NUM_SAMPLES; i++)
    {
        ivec2 curOffset = GetSampleOffset(i, BLOCKER_SEARCH_NUM_SAMPLES, BLOCKER_SEARCH_WIDTH);
        float curBlockerSample = texture(shadowMap, sampleCoords + RandomRotate(VogelDiskSample(i, BLOCKER_SEARCH_NUM_SAMPLES, InterleavedGradientNoise(gl_FragCoord.xy) * 2)) * BLOCKER_SEARCH_WIDTH * texelSize / frustumWidth).r;
        if (curDepth - bias > curBlockerSample)
        {
            numBlockers++;
            blockerAvg += curBlockerSample;
        }
    }
    
    if (numBlockers <= 0)
    {
        return 0;
    }
    if (numBlockers >= BLOCKER_SEARCH_WIDTH * BLOCKER_SEARCH_WIDTH)
    {
        return 1;
    }

    blockerAvg /= float(numBlockers);


    float sampleWidth = lightSizeUV * ((curDepth - blockerAvg) / (blockerAvg));
    // Apply PCF
    for (int i = 0; i < PCF_NUM_SAMPLES; ++i)
    {
        shadow += ((curDepth - bias) > (texture(shadowMap, sampleCoords + RandomRotate(VogelDiskSample(i, PCF_NUM_SAMPLES, InterleavedGradientNoise(gl_FragCoord.xy) * 2)) * sampleWidth * texelSize).r)) ? 1 : 0;
    }
    shadow /= PCF_NUM_SAMPLES;

    return shadow;
}

void main()
{
    // Texel color fetching from texture sampler
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 lightDot = vec3(0.0);
    vec3 normal = normalize(fragNormal);
    vec3 viewD = normalize(viewPos - fragPosition);
    vec3 specular = vec3(0.0);

    vec3 l = -lightDir;

    float NdotL = max(dot(normal, l), 0.0);
    lightDot += lightColor.rgb*NdotL;

    float specCo = 0.0;
    if (NdotL > 0.0) specCo = pow(max(0.0, dot(viewD, reflect(-(l), normal))), 16.0); // 16 refers to shine
    specular += specCo;

    finalColor = (texelColor*((colDiffuse + vec4(specular, 1.0))*vec4(lightDot, 1.0)));

    float shadow = GetShadow(normal, l);
    
    finalColor = mix(finalColor, vec4(0, 0, 0, 1), shadow);

    // Add ambient lighting whether in shadow or not
    finalColor += texelColor*(ambient/10.0)*colDiffuse;

    // Gamma correction
    finalColor = pow(finalColor, vec4(1.0/2.2));
    // finalColor = vec4(vec3(sampleWidth), 1);
}

#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Framework/Scene.h>
#include <Engine/Component/MeshRenderer.h>
#include <Engine/Component/Camera.h>
#include <Engine/Component/Controller.h>
#include <Engine/Component/AutoRotator.h>
#include <Engine/Component/DirectionalLight.h>
#include <Engine/Component/Atmosphere.h>
#include <Engine/Component/SkyLight.h>
#include <Engine/Component/GSplatRenderer.h>
#include <Engine/Engine.h>
#include <Engine/Texture/Texture2D.h>
#include <Engine/Texture/TextureCube.h>
#include <Engine/Context.h>
#include <Core/Common/Float16.h>

#include <AssetInterchange/GLTFLoader.h>

// already defined in GLTFLoader
// #define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <iostream>
#include <fstream>

using namespace hyp;
using namespace hyp::editor;

constexpr float3 kCubePositions[] =
{
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}, // +Z
    { 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}, // -Z
    { 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}, // +X
    {-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}, // -X
    {-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}, // +Y
    {-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}  // -Y
};

constexpr float3 kCubeNormals[] =
{
    { 0,  0,  1}, { 0,  0,  1}, { 0,  0,  1}, { 0,  0,  1}, // +Z
    { 0,  0, -1}, { 0,  0, -1}, { 0,  0, -1}, { 0,  0, -1}, // -Z
    { 1,  0,  0}, { 1,  0,  0}, { 1,  0,  0}, { 1,  0,  0}, // +X
    {-1,  0,  0}, {-1,  0,  0}, {-1,  0,  0}, {-1,  0,  0}, // -X
    { 0,  1,  0}, { 0,  1,  0}, { 0,  1,  0}, { 0,  1,  0}, // +Y
    { 0, -1,  0}, { 0, -1,  0}, { 0, -1,  0}, { 0, -1,  0}  // -Y
};

constexpr float4 kCubeTangents[] =
{
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, // +Z
    {-1,  0,  0,  1}, {-1,  0,  0,  1}, {-1,  0,  0,  1}, {-1,  0,  0,  1}, // -Z
    { 0,  0, -1,  1}, { 0,  0, -1,  1}, { 0,  0, -1,  1}, { 0,  0, -1,  1}, // +X
    { 0,  0,  1,  1}, { 0,  0,  1,  1}, { 0,  0,  1,  1}, { 0,  0,  1,  1}, // -X
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, // +Y
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}  // -Y
};

constexpr float2 kCubeTexCoord0s[] =
{
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +Z
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // -Z
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +X
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // -X
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +Y
    {0, 1}, {1, 1}, {1, 0}, {0, 0}  // -Y
};

constexpr uint16_t kCubeIndices[] =
{
    0, 1, 2,  0, 2, 3,       // +Z (Front)
    4, 5, 6,  4, 6, 7,       // -Z (Back)
    8, 9, 10, 8, 10, 11,     // +X (Right)
    12, 13, 14, 12, 14, 15,  // -X (Left)
    16, 17, 18, 16, 18, 19,  // +Y (Top)
    20, 21, 22, 20, 22, 23   // -Y (Bottom)
};

std::string vertexPositionOffsetSource = R"(
float3 evaluateVertexPositionOffset(in VertexPositionOffsetInput vpoi)
{
    return vpoi.normalLS * sin(vpoi.time) * 0.5;
}
)";

std::string materialSource = R"(
void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)
{
    float3 color = BaseColorFactor;
    float3 reflectDir = reflect(-mi.viewDirWS, mi.normalWS);
    float3 envColor = EnvMapTexture.SampleLevel(EnvMapSampler, reflectDir, 0.0).rgb;
    float3 texColor = BaseColorTexture.Sample(BaseColorSampler, mi.texCoords[0]).rgb;
    mo.baseColor = envColor * color * texColor;//lerp(float3(mi.texCoords[0], 1), color, float3(0.5));
}
)";

std::string translucentMaterialSource = R"(
void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)
{
    mo.baseColor = float3(0.8, 0.8, 0.8);
    mo.roughness = 0.05;
    mo.metallic = 0.0;
    mo.specular = 0.5;
    mo.opacity = 0.35;
}
)";

std::vector<SplatData> readSplatData(const std::filesystem::path& path)
{
    std::vector<SplatData> result;
    std::ifstream file(path, std::ios::binary);
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    result.resize(size / sizeof(SplatData));
    file.read(reinterpret_cast<char*>(result.data()), size);
    return result;
}

TRef<Mesh> createSphereMesh(float radius, uint32_t segments, uint32_t rings, Material* material)
{
    assert(segments >= 3 && rings >= 2);

    const uint32_t vertexCount = (segments + 1) * (rings + 1);
    std::vector<float3> positions;
    std::vector<float3> normals;
    std::vector<float4> tangents;
    std::vector<float2> texCoords;

    positions.reserve(vertexCount);
    normals.reserve(vertexCount);
    tangents.reserve(vertexCount);
    texCoords.reserve(vertexCount);

    for (uint32_t ring = 0; ring <= rings; ++ring)
    {
        const float phi = static_cast<float>(ring) / static_cast<float>(rings) * Math::PI;
        const float sinPhi = std::sin(phi);
        const float cosPhi = std::cos(phi);

        for (uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float theta = static_cast<float>(segment) / static_cast<float>(segments) * 2.0f * Math::PI;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float3 position = float3(
                radius * sinPhi * cosTheta,
                radius * cosPhi,
                radius * sinPhi * sinTheta
            );

            const float3 normal = Math::normalize(position);

            float3 tangent;
            if (std::abs(normal.y) > 0.999f)
            {
                tangent = float3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                tangent = Math::normalize(Math::cross(float3(0.0f, 1.0f, 0.0f), normal));
            }

            const float2 texCoord = float2(
                static_cast<float>(segment) / static_cast<float>(segments),
                static_cast<float>(ring) / static_cast<float>(rings)
            );

            positions.push_back(position);
            normals.push_back(normal);
            tangents.push_back(float4(tangent, 1.0f));
            texCoords.push_back(texCoord);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(rings) * segments * 6);

    for (uint32_t ring = 0; ring < rings; ++ring)
    {
        for (uint32_t segment = 0; segment < segments; ++segment)
        {
            const uint32_t i0 = ring * (segments + 1) + segment;
            const uint32_t i1 = i0 + segments + 1;
            const uint32_t i2 = i0 + 1;
            const uint32_t i3 = i1 + 1;

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);

            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    std::vector<uint8_t> vertexData;
    vertexData.resize(
        positions.size() * sizeof(float3) +
        normals.size() * sizeof(float3) +
        tangents.size() * sizeof(float4) +
        texCoords.size() * sizeof(float2)
    );

    size_t offset = 0;
    std::memcpy(vertexData.data() + offset, positions.data(), positions.size() * sizeof(float3));
    offset += positions.size() * sizeof(float3);
    std::memcpy(vertexData.data() + offset, normals.data(), normals.size() * sizeof(float3));
    offset += normals.size() * sizeof(float3);
    std::memcpy(vertexData.data() + offset, tangents.data(), tangents.size() * sizeof(float4));
    offset += tangents.size() * sizeof(float4);
    std::memcpy(vertexData.data() + offset, texCoords.data(), texCoords.size() * sizeof(float2));
    offset += texCoords.size() * sizeof(float2);

    std::vector<uint8_t> indexData;
    indexData.resize(indices.size() * sizeof(uint32_t));
    std::memcpy(indexData.data(), indices.data(), indexData.size());

    std::vector<VertexAttribute> vertexAttributes =
    {
        { VertexAttributeType::Position, sizeof(float3) },
        { VertexAttributeType::Normal, sizeof(float3) },
        { VertexAttributeType::Tangent, sizeof(float4) },
        { VertexAttributeType::TexCoord, sizeof(float2) },
    };

    TRef<Mesh> mesh = createObject<Mesh>(nullptr);
    uint32_t lodLevel = mesh->addLOD();
    mesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), IndexType::Uint32, 0.01f);
    mesh->addSubmesh(lodLevel, 0, static_cast<uint32_t>(positions.size()), 0, static_cast<uint32_t>(indices.size()), material);
    return mesh;
}

#define GEN_ASSET 0

int main()
{
    std::vector<uint8_t> vertexData;
    size_t vertexDataSize = std::size(kCubePositions) * sizeof(float3)
        + std::size(kCubeNormals) * sizeof(float3)
        + std::size(kCubeTangents) * sizeof(float4)
        + std::size(kCubeTexCoord0s) * sizeof(float2);
    vertexData.resize(vertexDataSize);
    size_t offset = 0;
    std::memcpy(vertexData.data() + offset, kCubePositions, std::size(kCubePositions) * sizeof(float3));
    offset += std::size(kCubePositions) * sizeof(float3);
    std::memcpy(vertexData.data() + offset, kCubeNormals, std::size(kCubeNormals) * sizeof(float3));
    offset += std::size(kCubeNormals) * sizeof(float3);
    std::memcpy(vertexData.data() + offset, kCubeTangents, std::size(kCubeTangents) * sizeof(float4));
    offset += std::size(kCubeTangents) * sizeof(float4);
    std::memcpy(vertexData.data() + offset, kCubeTexCoord0s, std::size(kCubeTexCoord0s) * sizeof(float2));
    offset += std::size(kCubeTexCoord0s) * sizeof(float2);

    std::vector<uint8_t> indexData;
    indexData.resize(std::size(kCubeIndices) * sizeof(uint16_t));
    std::memcpy(indexData.data(), kCubeIndices, std::size(kCubeIndices) * sizeof(uint16_t));

    Reflection::collectTypes();
    Engine engine;

    auto& assetManager = Context::get().getAssetManager();

    TRef<Scene> scene = createObject<Scene>(nullptr);

    std::vector<uint8_t> cubemapData;
    {
        std::vector<float3> hdrData;
        int hdrWidth, hdrHeight, hdrChannels;
        float* imageData = stbi_loadf(RESOURCE_DIR "/Test.hdr", &hdrWidth, &hdrHeight, &hdrChannels, 0);
        assert(imageData);
        hdrData.resize(hdrWidth * hdrHeight);
        std::memcpy(hdrData.data(), imageData, hdrData.size() * sizeof(float3));
        stbi_image_free(imageData);

        auto cubemap = equirectMapToCubemap(hdrData, hdrWidth, hdrHeight, 512, 500.0f);
        cubemapData.resize(cubemap.size() * sizeof(float16) * 3);
        for (size_t i = 0; i < cubemap.size(); ++i)
        {
            float3 color = cubemap[i];
            float16* data = reinterpret_cast<float16*>(cubemapData.data() + i * sizeof(float16) * 3);
            data[0] = float32ToFloat16(color.x);
            data[1] = float32ToFloat16(color.y);
            data[2] = float32ToFloat16(color.z);
        }
    }
    TRef<TextureCube> environmentMap = createObject<TextureCube>(nullptr);
    environmentMap->initialize(512, PixelFormat::RGB16_SFLOAT, false, CompressionPreset::HDR);
    environmentMap->setSourcePixels(std::move(cubemapData));

#if GEN_ASSET
    std::vector<uint8_t> imageMip0;
    int imageWidth, imageHeight, imageChannels;
    {
        uint8_t* imageData = stbi_load(RESOURCE_DIR "/brick_pavement_03_diff_1k.jpg", &imageWidth, &imageHeight, &imageChannels, 0);
        assert(imageData);
        imageMip0.resize(imageWidth * imageHeight * imageChannels);
        std::memcpy(imageMip0.data(), imageData, imageMip0.size());
        stbi_image_free(imageData);
    }

    

    TRef<Texture2D> texture2D = createObject<Texture2D>(nullptr);
    texture2D->initialize(imageWidth, imageHeight, imageChannels == 3 ? PixelFormat::RGB8_UNORM : PixelFormat::RGBA8_UNORM, true, CompressionPreset::Default, true);
    texture2D->setSourcePixels(std::move(imageMip0));
    SampleState sampleState;
    sampleState.filter = Filter::Anisotropic;
    texture2D->setSampleState(sampleState);
    Asset* texture2DAasset = assetManager.createAsset("Engine/TestTexture2D", texture2D);
    if (texture2DAasset)
        texture2DAasset->save();

    TRef<TextureCube> textureCube = createObject<TextureCube>(nullptr);
    textureCube->initialize(512, PixelFormat::RGB16_SFLOAT, false, CompressionPreset::HDR);
    textureCube->setSourcePixels(std::move(cubemapData));
    Asset* textureCubeAasset = assetManager.createAsset("Engine/TestTextureCube", textureCube);
    if (textureCubeAasset)
        textureCubeAasset->save();
#else
    Asset* texture2DAasset = assetManager.getAsset("Engine/TestTexture2D");
    TRef<Texture2D> texture2D = texture2DAasset->getOrLoadRootObject<Texture2D>();
    Asset* textureCubeAasset = assetManager.getAsset("Engine/TestTextureCube");
    TRef<TextureCube> textureCube = textureCubeAasset->getOrLoadRootObject<TextureCube>();
#endif

#if GEN_ASSET
    TRef<MaterialTemplate> materialTemplate = createObject<MaterialTemplate>(nullptr);
    materialTemplate->setMaterialSource(materialSource);
    materialTemplate->addParameter("BaseColorFactor", float3(1.0, 1.0, 1.0));
    materialTemplate->addParameter("BaseColor", texture2D.get());
    materialTemplate->addParameter("EnvMap", textureCube.get());
    Asset* materialTemplateAsset = assetManager.createAsset("Engine/TestMaterialTemplate", materialTemplate);
    if (materialTemplateAsset)
        materialTemplateAsset->save();

    TRef<MaterialInstance> materialInstance = createObject<MaterialInstance>(nullptr);
    materialInstance->setTemplate(materialTemplate);
    materialInstance->setParameterOverriddenValue("BaseColorFactor", float3(1.0, 0.0, 1.0));
    Asset* materialInstanceAsset = assetManager.createAsset("Engine/TestMaterialInstance", materialInstance);
    if (materialInstanceAsset)
        materialInstanceAsset->save();
#else
    Asset* materialTemplateAsset = assetManager.getAsset("Engine/TestMaterialTemplate");
    TRef<MaterialTemplate> materialTemplate = materialTemplateAsset->getOrLoadRootObject<MaterialTemplate>();
    Asset* materialInstanceAsset = assetManager.getAsset("Engine/TestMaterialInstance");
    TRef<MaterialInstance> materialInstance = materialInstanceAsset->getOrLoadRootObject<MaterialInstance>();
#endif
    //materialTemplate->setVertexPositionOffsetSource(vertexPositionOffsetSource);

#if GEN_ASSET
    TRef<Mesh> cubeMesh = createObject<Mesh>(nullptr);
    uint32_t lodLevel = cubeMesh->addLOD();
    std::vector<VertexAttribute> vertexAttributes =
    {
        { VertexAttributeType::Position, sizeof(float3) },
        { VertexAttributeType::Normal, sizeof(float3) },
        { VertexAttributeType::Tangent, sizeof(float4) },
        { VertexAttributeType::TexCoord, sizeof(float2) },
    };
    cubeMesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), IndexType::Uint16, 0.01f);
    cubeMesh->addSubmesh(
        lodLevel,
        0,
        static_cast<uint32_t>(std::size(kCubePositions)),
        0,
        static_cast<uint32_t>(std::size(kCubeIndices)),
        materialInstance
    );
    Asset* cubeMeshAsset = assetManager.createAsset("Engine/TestCubeMesh", cubeMesh);
    if (cubeMeshAsset)
        cubeMeshAsset->save();

    TRef<Mesh> sphereMesh = createSphereMesh(1.0f, 32, 16, materialTemplate);
    Asset* sphereMeshAsset = assetManager.createAsset("Engine/TestSphereMesh", sphereMesh);
    if (sphereMeshAsset)
        sphereMeshAsset->save();
#else
    Asset* cubeMeshAsset = assetManager.getAsset("Engine/TestCubeMesh");
    TRef<Mesh> cubeMesh = cubeMeshAsset->getOrLoadRootObject<Mesh>();
    Asset* sphereMeshAsset = assetManager.getAsset("Engine/TestSphereMesh");
    TRef<Mesh> sphereMesh = sphereMeshAsset->getOrLoadRootObject<Mesh>();
#endif

    GLTFLoader loader;
    auto loadResult = loader.load(RESOURCE_DIR "/Test.glb");
    if (loadResult)
    {
        for (auto& mesh : loadResult->meshes)
        {
            Entity* gltfMeshEntity = scene->createEntity("GLTFMesh");
            MeshRenderer* meshRenderer = gltfMeshEntity->addComponent<MeshRenderer>();
            meshRenderer->setMesh(mesh);
        }
    }

    Entity* player = scene->createEntity("Player");
    player->getTransform().setLocalPosition(double3(0, 0, 0));
    Camera* cameraComponent = player->addComponent<Camera>();
    Controller* controllerComponent = player->addComponent<Controller>();

    Entity* cube = scene->createEntity("Cube");
    cube->getTransform().setLocalPosition(double3(0, 10, 0));
    MeshRenderer* meshRenderer = cube->addComponent<MeshRenderer>();
    meshRenderer->setMesh(cubeMesh);
    cube->addComponent<AutoRotator>();

    player->addChild(cube, ReparentMode::Keep_World);

    Entity* sphere = scene->createEntity("Sphere");
    sphere->getTransform().setLocalPosition(double3(3, 8, 0));
    MeshRenderer* meshRenderer1 = sphere->addComponent<MeshRenderer>();
    meshRenderer1->setMesh(sphereMesh);

    // TranslucentPass 验证用: 半透明的玻璃球, 低 roughness 保证有明显的镜面高光,
    // 用来肉眼确认高光没有被 alpha 削弱
    TRef<MaterialTemplate> translucentMaterialTemplate = createObject<MaterialTemplate>(nullptr);
    translucentMaterialTemplate->setMaterialSource(translucentMaterialSource);
    translucentMaterialTemplate->setBlendMode(BlendMode::Translucent);
    TRef<MaterialInstance> translucentMaterialInstance = createObject<MaterialInstance>(nullptr);
    translucentMaterialInstance->setTemplate(translucentMaterialTemplate);
    TRef<Mesh> translucentSphereMesh = createSphereMesh(1.2f, 32, 16, translucentMaterialInstance);

    Entity* translucentSphere = scene->createEntity("TranslucentSphere");
    translucentSphere->getTransform().setLocalPosition(double3(1.5, 9, 1.5));
    MeshRenderer* translucentMeshRenderer = translucentSphere->addComponent<MeshRenderer>();
    translucentMeshRenderer->setMesh(translucentSphereMesh);

    Entity* light = scene->createEntity("Light");
    light->getTransform().setWorldRotationEuler(float3(-45.0f, 0.0f, 45.0f));
    DirectionalLight* dirLight = light->addComponent<DirectionalLight>();
    dirLight->setColor(float3(6.0, 6.0, 6.0));

    Entity* atmosphere = scene->createEntity("Atmosphere");
    Atmosphere* atmoComp = atmosphere->addComponent<Atmosphere>();

    Entity* skyLight = scene->createEntity("SkyLight");
    SkyLight* skyLightComp = skyLight->addComponent<SkyLight>();
    //skyLightComp->setLightSource(SkyLightSource::Environment_Map);
    //skyLightComp->setEnvironmentMap(environmentMap);

    std::vector<SplatData> splatData = readSplatData(RESOURCE_DIR "/plush.splat");
    TRef<GSplat> gsplat = createObject<GSplat>(nullptr);
    gsplat->setSplatData(std::move(splatData));

    Entity* gsplatEntity = scene->createEntity("GSplat");
    GSplatRenderer* gsplatRenderer = gsplatEntity->addComponent<GSplatRenderer>();
    gsplatRenderer->setGSplat(gsplat);

    engine.setScene(scene);
    scene->setPlayState(PlayState::Playing);
    engine.run();

    return 0;
}

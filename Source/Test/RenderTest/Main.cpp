#include <Engine/Material/MaterialTemplate.h>
#include <Engine/Material/MaterialInstance.h>
#include <Engine/Framework/Scene.h>
#include <Engine/Component/MeshRenderer.h>
#include <Engine/Component/Camera.h>
#include <Engine/Component/Controller.h>
#include <Engine/Component/AutoRotator.h>
#include <Engine/Component/DirectionalLight.h>
#include <Engine/Component/Atmosphere.h>
#include <Engine/Component/GSplatRenderer.h>
#include <RenderBase/RenderSystem.h>
#include <Engine/Engine.h>
#include <Engine/Texture/Texture2D.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <fstream>

using namespace hyp;

const std::vector<float3> positions =
{
    {-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}, // +Z
    { 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}, // -Z
    { 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}, // +X
    {-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}, // -X
    {-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}, // +Y
    {-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}  // -Y
};

const std::vector<float3> normals =
{
    { 0,  0,  1}, { 0,  0,  1}, { 0,  0,  1}, { 0,  0,  1}, // +Z
    { 0,  0, -1}, { 0,  0, -1}, { 0,  0, -1}, { 0,  0, -1}, // -Z
    { 1,  0,  0}, { 1,  0,  0}, { 1,  0,  0}, { 1,  0,  0}, // +X
    {-1,  0,  0}, {-1,  0,  0}, {-1,  0,  0}, {-1,  0,  0}, // -X
    { 0,  1,  0}, { 0,  1,  0}, { 0,  1,  0}, { 0,  1,  0}, // +Y
    { 0, -1,  0}, { 0, -1,  0}, { 0, -1,  0}, { 0, -1,  0}  // -Y
};

const std::vector<float4> tangents =
{
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, // +Z
    {-1,  0,  0,  1}, {-1,  0,  0,  1}, {-1,  0,  0,  1}, {-1,  0,  0,  1}, // -Z
    { 0,  0, -1,  1}, { 0,  0, -1,  1}, { 0,  0, -1,  1}, { 0,  0, -1,  1}, // +X
    { 0,  0,  1,  1}, { 0,  0,  1,  1}, { 0,  0,  1,  1}, { 0,  0,  1,  1}, // -X
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, // +Y
    { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}, { 1,  0,  0,  1}  // -Y
};

const std::vector<float2> texCoord0s =
{
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +Z
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // -Z
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +X
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // -X
    {0, 1}, {1, 1}, {1, 0}, {0, 0}, // +Y
    {0, 1}, {1, 1}, {1, 0}, {0, 0}  // -Y
};

const std::vector<uint16_t> indices =
{
    0, 1, 2,  0, 2, 3,       // +Z (Front)
    4, 5, 6,  4, 6, 7,       // -Z (Back)
    8, 9, 10, 8, 10, 11,     // +X (Right)
    12, 13, 14, 12, 14, 15,  // -X (Left)
    16, 17, 18, 16, 18, 19,  // +Y (Top)
    20, 21, 22, 20, 22, 23   // -Y (Bottom)
};

std::string materialSource = R"(
void evaluateMaterial(in MaterialInput mi, inout MaterialOutput mo)
{
    float3 color = BaseColorTexture.Sample(BaseColorSampler, mi.texCoord0).rgb;
    mo.baseColor = color * BaseColorFactor;//lerp(float3(mi.texCoord0, 1), color, float3(0.5));
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

int main()
{
    std::vector<uint8_t> vertexData;
    size_t vertexDataSize = positions.size() * sizeof(float3)
        + normals.size() * sizeof(float3)
        + tangents.size() * sizeof(float4)
        + texCoord0s.size() * sizeof(float2);
    vertexData.resize(vertexDataSize);
    size_t offset = 0;
    std::memcpy(vertexData.data() + offset, positions.data(), positions.size() * sizeof(float3));
    offset += positions.size() * sizeof(float3);
    std::memcpy(vertexData.data() + offset, normals.data(), normals.size() * sizeof(float3));
    offset += normals.size() * sizeof(float3);
    std::memcpy(vertexData.data() + offset, tangents.data(), tangents.size() * sizeof(float4));
    offset += tangents.size() * sizeof(float4);
    std::memcpy(vertexData.data() + offset, texCoord0s.data(), texCoord0s.size() * sizeof(float2));
    offset += texCoord0s.size() * sizeof(float2);

    std::vector<uint8_t> indexData;
    indexData.resize(indices.size() * sizeof(uint16_t));
    std::memcpy(indexData.data(), indices.data(), indices.size() * sizeof(uint16_t));

    int imageWidth, imageHeight, imageChannels;
    uint8_t* imageData = stbi_load(RESOURCE_DIR
        "/brick_pavement_03_diff_1k.jpg", &imageWidth, &imageHeight, &imageChannels, 0);
    assert(imageData);
    std::vector<uint8_t> imageMip0(imageWidth * imageHeight * imageChannels);
    std::memcpy(imageMip0.data(), imageData, imageMip0.size());
    stbi_image_free(imageData);

    Engine engine;
    TRef<Scene> scene = createObject<Scene>(nullptr);
    TRef<Texture2D> texture = createObject<Texture2D>(nullptr);
    texture->initialize(imageWidth, imageHeight, imageChannels == 3 ? PixelFormat::RGB8_UNORM : PixelFormat::RGBA8_UNORM, true, CompressionPreset::Default, true);
    texture->setSourcePixels(std::move(imageMip0));

    texture->uploadToGpu();
    SampleState sampleState;
    sampleState.filter = Filter::Anisotropic;
    texture->setSampleState(sampleState);

    TRef<MaterialTemplate> material = createObject<MaterialTemplate>(nullptr);
    material->setMaterialSource(materialSource);
    material->addParameter("BaseColorFactor", float3(1.0, 1.0, 1.0));
    material->addParameter("BaseColor", texture.get());

    TRef<MaterialInstance> materialInstance = createObject<MaterialInstance>(nullptr);
    materialInstance->setTemplate(material);
    materialInstance->setParameterOverriddenValue("BaseColorFactor", float3(1.0, 0.0, 1.0));

    TRef<Mesh> mesh = createObject<Mesh>(nullptr);
    uint32_t lodLevel = mesh->addLOD();
    std::vector<Mesh::VertexAttribute> vertexAttributes =
    {
        { Mesh::VertexAttributeType::Position, sizeof(float3) },
        { Mesh::VertexAttributeType::Normal, sizeof(float3) },
        { Mesh::VertexAttributeType::Tangent, sizeof(float4) },
        { Mesh::VertexAttributeType::TexCoord, sizeof(float2) },
    };
    mesh->setLODData(lodLevel, std::move(vertexData), std::move(vertexAttributes), std::move(indexData), Mesh::IndexType::Uint16, 0.01f);
    mesh->addSubmesh(
        lodLevel,
        0,
        static_cast<uint32_t>(positions.size()),
        0,
        static_cast<uint32_t>(indices.size()),
        materialInstance
    );
    Entity* player = scene->createEntity("Player");
    player->getTransform().setLocalPosition(double3(0, 0, 0));
    Camera* cameraComponent = player->addComponent<Camera>();
    Controller* controllerComponent = player->addComponent<Controller>();

    Entity* cube = scene->createEntity("Cube");
    cube->getTransform().setLocalPosition(double3(0, 10, 0));
    MeshRenderer* meshRenderer = cube->addComponent<MeshRenderer>();
    meshRenderer->setMesh(mesh);
    cube->addComponent<AutoRotator>();

    player->addChild(cube, ReparentMode::Keep_World);

    Entity* cube1 = scene->createEntity("Cube_1");
    cube1->getTransform().setLocalPosition(double3(3, 8, 0));
    MeshRenderer* meshRenderer1 = cube1->addComponent<MeshRenderer>();
    meshRenderer1->setMesh(mesh);

    Entity* light = scene->createEntity("Light");
    DirectionalLight* dirLight = light->addComponent<DirectionalLight>();

    Entity* atmosphere = scene->createEntity("Atmosphere");
    Atmosphere* atmoComp = atmosphere->addComponent<Atmosphere>();

    std::vector<SplatData> splatData = readSplatData(RESOURCE_DIR R"(/plush.splat)");
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

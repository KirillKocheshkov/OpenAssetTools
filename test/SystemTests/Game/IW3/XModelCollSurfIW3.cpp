#include "Game/IW3/IW3.h"
#include "Gdt/GdtLookup.h"
#include "IObjLoader.h"
#include "SearchPath/MockSearchPath.h"
#include "XModel/Gltf/GltfBinOutput.h"
#include "XModel/Gltf/GltfWriter.h"
#include "XModel/XModelCommon.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <nlohmann/json.hpp>
#include <sstream>

namespace
{
    TEST_CASE("IW3 model collision lod produces collSurfs with correct plane/svec/tvec", "[iw3][system][xmodel][collision]")
    {
        XModelCommon common;
        common.m_name = "triangle";
        common.m_vertices = {
            {.coordinates = {0, 0, 0}, .normal = {0, 0, 1}, .color = {1, 1, 1, 1}, .uv = {0, 0}},
            {.coordinates = {4, 0, 0}, .normal = {0, 0, 1}, .color = {1, 1, 1, 1}, .uv = {1, 0}},
            {.coordinates = {0, 3, 0}, .normal = {0, 0, 1}, .color = {1, 1, 1, 1}, .uv = {0, 1}},
        };
        XModelMaterial material{};
        material.ApplyDefaults();
        material.name = "material";
        common.m_materials.push_back(material);
        common.m_objects.push_back({.name = "triangle", .materialIndex = 0, .m_faces = {XModelFace{{0, 1, 2}}}});

        std::ostringstream modelData(std::ios::binary);
        const gltf::BinOutput modelOutput(modelData);
        gltf::Writer::CreateWriter(&modelOutput, "iw3", "test")->Write(common);

        const nlohmann::json metadata{
            {"_game",    "iw3"                                          },
            {"_type",    "xmodel"                                       },
            {"_version", 2                                              },
            {"collLod",  0                                              },
            {"flags",    0                                              },
            {"lods",     {{{"file", "triangle.glb"}, {"distance", 100}}}},
        };

        MockSearchPath searchPath;
        searchPath.AddFileData("triangle.glb", modelData.str());
        searchPath.AddFileData("xmodel/triangle.json", metadata.dump());
        searchPath.AddFileData("partclassification.csv", "");

        Zone zone("collsurf", 0, GameId::IW3, GamePlatform::PC);
        AssetCreatorCollection creators(zone);
        GdtLookup gdt;
        IObjLoader::GetObjLoaderForGame(GameId::IW3)->ConfigureCreatorCollection(creators, zone, searchPath, gdt);
        IgnoredAssetLookup ignored;
        ignored.m_ignored_asset_lookup.emplace("material", IW3::ASSET_TYPE_MATERIAL);
        AssetCreationContext context(zone, &creators, &ignored);

        REQUIRE(creators.CreateAsset(IW3::ASSET_TYPE_XMODEL, "triangle", context).HasBeenSuccessful());
        const auto* asset = zone.m_pools.GetAsset<IW3::AssetXModel>("triangle");
        REQUIRE(asset);
        const auto& model = *asset->Asset();

        REQUIRE(model.collLod == 0);
        REQUIRE(model.numCollSurfs == 1);
        REQUIRE(model.collSurfs != nullptr);

        const auto& surf = model.collSurfs[0];
        CHECK(surf.boneIdx == 0);
        REQUIRE(surf.numCollTris == 1);
        REQUIRE(surf.collTris != nullptr);

        const auto& tri = surf.collTris[0];

        // Expected plane: normalize(cross(v2-v0, v1-v0)) with v0=(0,0,0), v1=(4,0,0), v2=(0,3,0) -> normal (0,0,-1), d=0
        // (cross(e2,e1), not cross(e1,e2) - matches the convention recovered from xmodel.dll's collvec.cpp)
        CHECK_THAT(tri.plane[0], Catch::Matchers::WithinAbs(0.0f, 0.0001f));
        CHECK_THAT(tri.plane[1], Catch::Matchers::WithinAbs(0.0f, 0.0001f));
        CHECK_THAT(tri.plane[2], Catch::Matchers::WithinAbs(-1.0f, 0.0001f));
        CHECK_THAT(tri.plane[3], Catch::Matchers::WithinAbs(0.0f, 0.0001f));

        // Convention verified against real retail collSurfs (stored xyz is negated relative to the naive
        // barycentric form): s = svec.w - dot(v, svec.xyz) must be 0 at v0/v2 (the edge svec was built from)
        // and -1 at v1 (the opposite vertex).
        const auto evalVec = [](const float* vec, const float x, const float y, const float z)
        { return vec[3] - (vec[0] * x + vec[1] * y + vec[2] * z); };

        CHECK_THAT(evalVec(tri.svec, 0, 0, 0), Catch::Matchers::WithinAbs(0.0f, 0.0001f));
        CHECK_THAT(evalVec(tri.svec, 4, 0, 0), Catch::Matchers::WithinAbs(-1.0f, 0.0001f));
        CHECK_THAT(evalVec(tri.svec, 0, 3, 0), Catch::Matchers::WithinAbs(0.0f, 0.0001f));

        CHECK_THAT(evalVec(tri.tvec, 0, 0, 0), Catch::Matchers::WithinAbs(0.0f, 0.0001f));
        CHECK_THAT(evalVec(tri.tvec, 4, 0, 0), Catch::Matchers::WithinAbs(0.0f, 0.0001f));
        CHECK_THAT(evalVec(tri.tvec, 0, 3, 0), Catch::Matchers::WithinAbs(-1.0f, 0.0001f));

        CHECK(model.contents == surf.contents);
    }
} // namespace

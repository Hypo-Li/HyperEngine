#include "FontBake.h"

#include <Core/Common/ModuleServiceRegistry.h>

#include <HypImGui/ImGuiEnumWidgets.h>
#include <HypImGui/ImGuiPropertyWidgets.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <iterator>
#include <string>

namespace hyp::ed
{
    FontBakeJob::FontBakeJob(std::vector<uint8_t> faceData, FontBakeSettings settings) :
        mFaceData(std::move(faceData)),
        mSettings(std::move(settings))
    {
        IFontUtility* fontUtility = ModuleServiceRegistry::get().getService<IFontUtility>();
        if (!fontUtility)
        {
            mFinished.store(true, std::memory_order_release);
            return;
        }

        mThread = std::jthread([this, fontUtility]()
        {
            mResult = fontUtility->bake(mFaceData, mSettings,
                [this](float progress)
                {
                    mProgress.store(progress, std::memory_order_relaxed);
                    return !mCancelled.load(std::memory_order_relaxed);
                });
            mFinished.store(true, std::memory_order_release);
        });
    }

    FontBakeJob::~FontBakeJob()
    {
        cancel();
    }

    bool DrawFontBakeSettings(FontBakeSettings& settings)
    {
        bool changed = false;
        if (!BeginPropertyTable("FontBakeSettings", ImGuiTableFlags_BordersInnerH))
            return false;

        BeginPropertyRow("SDF Type");
        changed |= EnumCombo("##SdfType", &settings.sdfType);

        BeginPropertyRow("Face Index");
        int faceIndex = static_cast<int>(settings.faceIndex);
        if (ImGui::InputInt("##FaceIndex", &faceIndex))
        {
            settings.faceIndex = static_cast<uint32_t>(std::max(faceIndex, 0));
            changed = true;
        }

        BeginPropertyRow("Bake Ppem");
        int ppem = static_cast<int>(settings.bakePpem);
        if (ImGui::DragInt("##BakePpem", &ppem, 1.0f, 8, 256))
        {
            settings.bakePpem = clampFontBakePpem(static_cast<uint32_t>(ppem));
            changed = true;
        }

        BeginPropertyRow("Spread Texels");
        if (ImGui::DragFloat("##SpreadTexels", &settings.spreadTexels, 0.1f, 1.0f, static_cast<float>(clampFontBakePpem(settings.bakePpem)), "%.1f"))
            changed = true;

        BeginPropertyRow("Page Size");
        {
            constexpr uint32_t kPageSizes[] = { 256, 512, 1024, 2048, 4096 };
            const std::string preview = std::to_string(settings.pageSize);
            if (ImGui::BeginCombo("##PageSize", preview.c_str()))
            {
                for (uint32_t size : kPageSizes)
                {
                    if (ImGui::Selectable(std::to_string(size).c_str(), size == settings.pageSize))
                    {
                        settings.pageSize = size;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }

        BeginPropertyRow("Padding");
        int padding = static_cast<int>(settings.padding);
        if (ImGui::DragInt("##Padding", &padding, 1.0f, 0, 64))
        {
            settings.padding = static_cast<uint32_t>(padding);
            changed = true;
        }

        BeginPropertyRow("Bake Kerning");
        changed |= ImGui::Checkbox("##BakeKerning", &settings.bakeKerning);

        BeginPropertyRow("Include ASCII");
        changed |= ImGui::Checkbox("##IncludeAscii", &settings.includeAscii);

        BeginPropertyRow("Characters");
        changed |= ImGui::InputTextMultiline("##Characters", &settings.characters, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f));

        BeginPropertyRow("Unicode Ranges");
        {
            int removeIndex = -1;
            for (size_t i = 0; i < settings.unicodeRanges.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                FontCharsetRange& range = settings.unicodeRanges[i];
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
                changed |= ImGui::InputScalar("##First", ImGuiDataType_U32, &range.first, nullptr, nullptr, "%X", ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
                changed |= ImGui::InputScalar("##Last", ImGuiDataType_U32, &range.last, nullptr, nullptr, "%X", ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    removeIndex = static_cast<int>(i);
                ImGui::PopID();
            }
            if (removeIndex >= 0)
            {
                settings.unicodeRanges.erase(settings.unicodeRanges.begin() + removeIndex);
                changed = true;
            }
            if (ImGui::Button("Add Range"))
            {
                settings.unicodeRanges.push_back({});
                changed = true;
            }
        }

        BeginPropertyRow("Chars Directory");
        changed |= ImGui::InputText("##CharsDirectory", &settings.charsDirectory);

        BeginPropertyRow("Chars Extensions");
        changed |= ImGui::InputText("##CharsExtensions", &settings.charsExtensions);

        EndPropertyTable();
        return changed;
    }
}

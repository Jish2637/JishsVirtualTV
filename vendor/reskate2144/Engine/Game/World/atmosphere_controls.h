#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace dingosdk {
enum class AtmosphereType { number, integer, toggle, enumeration, vector, color, texture };
struct AtmosphereOption { std::uint32_t value; const char* label; };
struct AtmosphereControl {
    const char *key, *label, *section;
    AtmosphereType type;
    unsigned property, offset, lanes, option_start, option_count;
    double minimum, maximum;
};
inline constexpr std::array<const char*, 3> atmosphere_groups{"Fog", "Sky", "Wind"};
inline constexpr unsigned atmosphere_kind(unsigned property) { return property < 80 ? 0 : property < 184 ? 1 : 2; }
inline constexpr std::array<AtmosphereOption, 45> atmosphere_options{{
    {0,"None"},
    {1,"Corner"},
    {0,"Absorption Scattering"},
    {1,"Extinction Albedo"},
    {0,"Absorption Scattering"},
    {1,"Extinction Albedo"},
    {0,"Temporal Method Exponential"},
    {1,"Temporal Method Neighborhood Clip"},
    {2,"High Resolution 2"},
    {4,"Medium Resolution 4"},
    {8,"Low Resolution 8"},
    {16,"Low Resolution 16"},
    {32,"Low Resolution 32"},
    {2,"High Resolution 2"},
    {4,"Medium Resolution 4"},
    {8,"Low Resolution 8"},
    {16,"Low Resolution 16"},
    {32,"Low Resolution 32"},
    {2,"High Resolution 2"},
    {4,"Medium Resolution 4"},
    {8,"Low Resolution 8"},
    {16,"Low Resolution 16"},
    {32,"Low Resolution 32"},
    {2,"High Resolution 2"},
    {4,"Medium Resolution 4"},
    {8,"Low Resolution 8"},
    {16,"Low Resolution 16"},
    {32,"Low Resolution 32"},
    {2,"High Resolution 2"},
    {4,"Medium Resolution 4"},
    {8,"Low Resolution 8"},
    {16,"Low Resolution 16"},
    {32,"Low Resolution 32"},
    {0,"Procedural"},
    {1,"Hdri"},
    {2,"Physical"},
    {0,"Disabled"},
    {1,"Clear"},
    {2,"Cloud Layer Only"},
    {3,"Mask Only"},
    {4,"Cloud Layer And Mask"},
    {5,"Panoramic Only"},
    {6,"Cloud Layer And Panoramic"},
    {7,"Mask And Panoramic"},
    {8,"Cloud Layer And Mask And Panoramic"},
}};
inline constexpr std::array<AtmosphereControl, 214> atmosphere_controls{{
    {"Fog.Enable","Enable","Fog",AtmosphereType::toggle,0,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogDistanceMultiplier","Fog Distance Multiplier","Fog",AtmosphereType::number,1,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogGradientEnable","Fog Gradient Enable","Fog",AtmosphereType::toggle,2,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.Start","Start","Fog",AtmosphereType::number,3,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.End","End","Fog",AtmosphereType::number,4,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.Curve","Curve","Fog",AtmosphereType::vector,5,0,4,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogGradientHeightFadeEnable","Fog Gradient Height Fade Enable","Fog",AtmosphereType::toggle,6,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FadeStart","Fade Start","Fog",AtmosphereType::number,7,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FadeEnd","Fade End","Fog",AtmosphereType::number,8,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogColorEnable","Fog Color Enable","Fog",AtmosphereType::toggle,9,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogColor","Fog Color","Fog",AtmosphereType::color,10,0,3,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogColorStart","Fog Color Start","Fog",AtmosphereType::number,11,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogColorEnd","Fog Color End","Fog",AtmosphereType::number,12,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.FogColorCurve","Fog Color Curve","Fog",AtmosphereType::vector,13,0,4,0,0,-1000000000.0,1000000000.0},
    {"Fog.TransparencyFadeClamp","Transparency Fade Clamp","Transparency",AtmosphereType::number,14,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.TransparencyFadeStart","Transparency Fade Start","Transparency",AtmosphereType::number,15,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.TransparencyFadeEnd","Transparency Fade End","Transparency",AtmosphereType::number,16,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.TransparencyFadeCurve","Transparency Fade Curve","Transparency",AtmosphereType::vector,17,0,4,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringEnabled","Forward Light Scattering Enabled","Light scattering",AtmosphereType::toggle,18,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringPhaseG","Forward Light Scattering Phase G","Light scattering",AtmosphereType::number,19,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringStrength","Forward Light Scattering Strength","Light scattering",AtmosphereType::number,20,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringColor","Forward Light Scattering Color","Light scattering",AtmosphereType::color,21,0,3,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringPresence","Forward Light Scattering Presence","Light scattering",AtmosphereType::number,22,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringMaxBlurLength","Forward Light Scattering Max Blur Length","Light scattering",AtmosphereType::number,23,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringExtinction","Forward Light Scattering Extinction","Light scattering",AtmosphereType::number,24,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringSmoothness","Forward Light Scattering Smoothness","Light scattering",AtmosphereType::number,25,0,1,0,0,-1000000000.0,1000000000.0},
    {"Fog.ForwardLightScatteringAttenuationType","Forward Light Scattering Attenuation Type","Light scattering",AtmosphereType::enumeration,26,0,1,0,2,-1000000000.0,1000000000.0},
    {"Fog.HeightFogEnable","Height Fog Enable","Height fog",AtmosphereType::toggle,27,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogFollowCamera","Height Fog Follow Camera","Height fog",AtmosphereType::number,28,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogAltitude","Height Fog Altitude","Height fog",AtmosphereType::number,29,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogDepth","Height Fog Depth","Height fog",AtmosphereType::number,30,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogVisibilityRange","Height Fog Visibility Range","Height fog",AtmosphereType::number,31,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogStart","Height Fog Start","Height fog",AtmosphereType::number,32,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogEnd","Height Fog End","Height fog",AtmosphereType::number,33,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogCurve","Height Fog Curve","Height fog",AtmosphereType::vector,34,0,4,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaEnable","Participating Media Enable","Participating media",AtmosphereType::toggle,35,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaSun","Participating Media Sun","Participating media",AtmosphereType::toggle,36,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.SunLightScatteringIntensity","Sun Light Scattering Intensity","Participating media",AtmosphereType::number,37,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaLocalLights","Participating Media Local Lights","Participating media",AtmosphereType::toggle,38,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.LocalLightScatteringIntensity","Local Light Scattering Intensity","Participating media",AtmosphereType::number,39,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaExtinctionEnable","Participating Media Extinction Enable","Participating media",AtmosphereType::toggle,40,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaEmissiveAndAmbientEnable","Participating Media Emissive And Ambient Enable","Participating media",AtmosphereType::toggle,41,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.LocalLightVolumetricShadowEnable","Local Light Volumetric Shadow Enable","Quality & shadows",AtmosphereType::toggle,42,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.SunVolumetricShadowEnable","Sun Volumetric Shadow Enable","Quality & shadows",AtmosphereType::toggle,43,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.CloudShadowEnable","Cloud Shadow Enable","Quality & shadows",AtmosphereType::toggle,44,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.CloudShadowIntensityRange","Cloud Shadow Intensity Range","Quality & shadows",AtmosphereType::vector,45,0,2,2,0,-1000000000.0,1000000000.0},
    {"Fog.LocalParticipatingMediaVolumesEnable","Local Participating Media Volumes Enable","Participating media",AtmosphereType::toggle,46,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaDefaultPhase","Participating Media Default Phase","Participating media",AtmosphereType::number,47,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaFadeStart","Participating Media Fade Start","Participating media",AtmosphereType::number,48,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaFadeEnd","Participating Media Fade End","Participating media",AtmosphereType::number,49,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaFadeCurve","Participating Media Fade Curve","Participating media",AtmosphereType::vector,50,0,4,2,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaFromGlobalFog","Participating Media From Global Fog","Participating media",AtmosphereType::toggle,51,0,1,2,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.SpecificationMode","Depth Fog Participating Media / Specification Mode","Depth media",AtmosphereType::enumeration,52,52,1,2,2,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Absorption","Depth Fog Participating Media / Absorption","Depth media",AtmosphereType::number,52,60,1,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Scattering","Depth Fog Participating Media / Scattering","Depth media",AtmosphereType::vector,52,32,3,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Exctinction","Depth Fog Participating Media / Extinction","Depth media",AtmosphereType::number,52,48,1,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Albedo","Depth Fog Participating Media / Albedo","Depth media",AtmosphereType::color,52,16,3,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Emissive","Depth Fog Participating Media / Emissive","Depth media",AtmosphereType::color,52,0,3,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.BackgroundOcclusionScale","Depth Fog Participating Media / Background Occlusion Scale","Depth media",AtmosphereType::number,52,64,1,4,0,-1000000000.0,1000000000.0},
    {"Fog.DepthFogParticipatingMedia.Phase","Depth Fog Participating Media / Phase","Depth media",AtmosphereType::number,52,56,1,4,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.SpecificationMode","Height Fog Participating Media / Specification Mode","Height media",AtmosphereType::enumeration,53,52,1,4,2,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Absorption","Height Fog Participating Media / Absorption","Height media",AtmosphereType::number,53,60,1,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Scattering","Height Fog Participating Media / Scattering","Height media",AtmosphereType::vector,53,32,3,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Exctinction","Height Fog Participating Media / Extinction","Height media",AtmosphereType::number,53,48,1,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Albedo","Height Fog Participating Media / Albedo","Height media",AtmosphereType::color,53,16,3,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Emissive","Height Fog Participating Media / Emissive","Height media",AtmosphereType::color,53,0,3,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.BackgroundOcclusionScale","Height Fog Participating Media / Background Occlusion Scale","Height media",AtmosphereType::number,53,64,1,6,0,-1000000000.0,1000000000.0},
    {"Fog.HeightFogParticipatingMedia.Phase","Height Fog Participating Media / Phase","Height media",AtmosphereType::number,53,56,1,6,0,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaTemporalMethod","Participating Media Temporal Method","Quality & shadows",AtmosphereType::enumeration,54,0,1,6,2,-1000000000.0,1000000000.0},
    {"Fog.ParticipatingMediaTemporalFilterStrength","Participating Media Temporal Filter Strength","Quality & shadows",AtmosphereType::number,55,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.LocalVolumeViewDistance","Local Volume View Distance","Quality & shadows",AtmosphereType::number,56,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.LocalVolumeReflectionViewDistance","Local Volume Reflection View Distance","Quality & shadows",AtmosphereType::number,57,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.DepthSliceUseLogDistribution","Depth Slice Use Log Distribution","Quality & shadows",AtmosphereType::toggle,58,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.DepthSliceLogGradient","Depth Slice Log Gradient","Quality & shadows",AtmosphereType::number,59,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.DepthSliceDistributionExponent","Depth Slice Distribution Exponent","Quality & shadows",AtmosphereType::number,60,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.LocalVolumeFarFadeLength","Local Volume Far Fade Length","Quality & shadows",AtmosphereType::number,61,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.LocalVolumeMediaFarFadeCurve","Local Volume Media Far Fade Curve","Quality & shadows",AtmosphereType::vector,62,0,4,8,0,-1000000000.0,1000000000.0},
    {"Fog.ExtinctionCascadeBaseVoxelSize","Extinction Cascade Base Voxel Size","Quality & shadows",AtmosphereType::number,63,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.ExtinctionCascadeVoxelSizeCascadeFactor","Extinction Cascade Voxel Size Cascade Factor","Quality & shadows",AtmosphereType::number,64,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.ExtinctionCascadeResolution","Extinction Cascade Resolution","Quality & shadows",AtmosphereType::integer,65,0,1,8,0,1.0,512.0},
    {"Fog.VolumetricShadowmapResolution","Volumetric Shadowmap Resolution","Quality & shadows",AtmosphereType::integer,66,0,1,8,0,1.0,4096.0},
    {"Fog.VolumetricShadowmapMaxCount","Volumetric Shadowmap Max Count","Quality & shadows",AtmosphereType::integer,67,0,1,8,0,0.0,64.0},
    {"Fog.SunVolumetricShadowSampleCount","Sun Volumetric Shadow Sample Count","Quality & shadows",AtmosphereType::integer,68,0,1,8,0,1.0,256.0},
    {"Fog.SunVolumetricShadowSampleStrech","Sun Volumetric Shadow Sample Stretch","Quality & shadows",AtmosphereType::number,69,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.SunExponentialShadowMapEnable","Sun Exponential Shadow Map Enable","Quality & shadows",AtmosphereType::toggle,70,0,1,8,0,-1000000000.0,1000000000.0},
    {"Fog.ResolutionScale.Low","Resolution Scale / Low","Quality & shadows",AtmosphereType::enumeration,71,8,1,8,5,-1000000000.0,1000000000.0},
    {"Fog.ResolutionScale.Medium","Resolution Scale / Medium","Quality & shadows",AtmosphereType::enumeration,71,0,1,13,5,-1000000000.0,1000000000.0},
    {"Fog.ResolutionScale.High","Resolution Scale / High","Quality & shadows",AtmosphereType::enumeration,71,4,1,18,5,-1000000000.0,1000000000.0},
    {"Fog.ResolutionScale.Ultra","Resolution Scale / Ultra","Quality & shadows",AtmosphereType::enumeration,71,12,1,23,5,-1000000000.0,1000000000.0},
    {"Fog.DepthSlices.Low","Depth Slices / Low","Quality & shadows",AtmosphereType::integer,72,0,1,28,0,1.0,4096.0},
    {"Fog.DepthSlices.Medium","Depth Slices / Medium","Quality & shadows",AtmosphereType::integer,72,4,1,28,0,1.0,4096.0},
    {"Fog.DepthSlices.High","Depth Slices / High","Quality & shadows",AtmosphereType::integer,72,8,1,28,0,1.0,4096.0},
    {"Fog.DepthSlices.Ultra","Depth Slices / Ultra","Quality & shadows",AtmosphereType::integer,72,12,1,28,0,1.0,4096.0},
    {"Fog.ReflectionResolutionScale","Reflection Resolution Scale","Quality & shadows",AtmosphereType::enumeration,73,0,1,28,5,-1000000000.0,1000000000.0},
    {"Fog.ReflectionDepthSlices","Reflection Depth Slices","Quality & shadows",AtmosphereType::integer,74,0,1,33,0,1.0,4096.0},
    {"Fog.GlobalVolumeEnable","Global Volume Enable","Participating media",AtmosphereType::toggle,75,0,1,33,0,-1000000000.0,1000000000.0},
    {"Fog.GlobalVolumeViewDistance","Global Volume View Distance","Quality & shadows",AtmosphereType::number,76,0,1,33,0,-1000000000.0,1000000000.0},
    {"Fog.GlobalVolumeReflectionViewDistance","Global Volume Reflection View Distance","Quality & shadows",AtmosphereType::number,77,0,1,33,0,-1000000000.0,1000000000.0},
    {"Fog.DepthSliceDistributionExponentGlobal","Depth Slice Distribution Exponent Global","Quality & shadows",AtmosphereType::number,78,0,1,33,0,-1000000000.0,1000000000.0},
    {"Fog.DepthSliceDensityRatioLocalGlobal","Depth Slice Density Ratio Local Global","Quality & shadows",AtmosphereType::number,79,0,1,33,0,-1000000000.0,1000000000.0},
    {"Sky.Enable","Enable","Sky",AtmosphereType::toggle,80,0,1,33,0,-1000000000.0,1000000000.0},
    {"Sky.DrawSkyGeo","Draw Sky Geo","Sky",AtmosphereType::toggle,81,0,1,33,0,-1000000000.0,1000000000.0},
    {"Sky.CaptureForceLowerColorEnabled","Capture Force Lower Color Enabled","Environment map",AtmosphereType::toggle,82,0,1,33,0,-1000000000.0,1000000000.0},
    {"Sky.CaptureForceLowerColor","Capture Force Lower Color","Environment map",AtmosphereType::color,83,0,3,33,0,-1000000000.0,1000000000.0},
    {"Sky.SkyType","Sky Type","Sky",AtmosphereType::enumeration,84,0,1,33,3,-1000000000.0,1000000000.0},
    {"Sky.LuminanceScale","Luminance Scale","Sky",AtmosphereType::number,85,0,1,36,0,-1000000000.0,1000000000.0},
    {"Sky.SkyGradientTexture","Sky Gradient Texture","Sky",AtmosphereType::texture,86,0,1,36,0,-1000000000.0,1000000000.0},
    {"Sky.AlphaOutput","Alpha Output","Sky",AtmosphereType::enumeration,87,0,1,36,9,-1000000000.0,1000000000.0},
    {"Sky.AllowEnergyIncreaseInNormalization","Allow Energy Increase In Normalization","Sky",AtmosphereType::toggle,88,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.HdriRotation","Hdri Rotation","HDRI",AtmosphereType::number,89,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.HdriTexture","Hdri Texture","HDRI",AtmosphereType::texture,90,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.SunSize","Sun Size","Sky",AtmosphereType::number,91,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.SunScale","Sun Scale","Sky",AtmosphereType::number,92,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicUVMinX","Panoramic UV Min X","Panorama",AtmosphereType::number,93,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicUVMaxX","Panoramic UV Max X","Panorama",AtmosphereType::number,94,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicUVMinY","Panoramic UV Min Y","Panorama",AtmosphereType::number,95,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicUVMaxY","Panoramic UV Max Y","Panorama",AtmosphereType::number,96,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicTileFactor","Panoramic Tile Factor","Panorama",AtmosphereType::number,97,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicRotation","Panoramic Rotation","Panorama",AtmosphereType::number,98,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicTexture","Panoramic Texture","Panorama",AtmosphereType::texture,99,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicAlphaTexture","Panoramic Alpha Texture","Panorama",AtmosphereType::texture,100,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicAlphaScale","Panoramic Alpha Scale","Panorama",AtmosphereType::number,101,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.PanoramicAlphaBias","Panoramic Alpha Bias","Panorama",AtmosphereType::number,102,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.SkyGradientFollowsPanoramicUVs","Sky Gradient Follows Panoramic U Vs","Sky",AtmosphereType::toggle,103,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowPeriod","Flow Period","Flow",AtmosphereType::number,104,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowDistance","Flow Distance","Flow",AtmosphereType::number,105,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowDirection","Flow Direction","Flow",AtmosphereType::number,106,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowHeightMaskScale","Flow Height Mask Scale","Flow",AtmosphereType::number,107,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowHeightMaskBias","Flow Height Mask Bias","Flow",AtmosphereType::number,108,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FlowMaskTexture","Flow Mask Texture","Flow",AtmosphereType::texture,109,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayerSunColor","Cloud Layer Sun Color","Cloud lighting",AtmosphereType::color,110,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayerMaskTexture","Cloud Layer Mask Texture","Cloud lighting",AtmosphereType::texture,111,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Altitude","Cloud Layer1 Altitude","Cloud layer 1",AtmosphereType::number,112,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1TileFactor","Cloud Layer1 Tile Factor","Cloud layer 1",AtmosphereType::number,113,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Rotation","Cloud Layer1 Rotation","Cloud layer 1",AtmosphereType::number,114,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Speed","Cloud Layer1 Speed","Cloud layer 1",AtmosphereType::number,115,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1SunLightIntensity","Cloud Layer1 Sun Light Intensity","Cloud layer 1",AtmosphereType::number,116,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1SunLightPower","Cloud Layer1 Sun Light Power","Cloud layer 1",AtmosphereType::number,117,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1AmbientLightIntensity","Cloud Layer1 Ambient Light Intensity","Cloud layer 1",AtmosphereType::number,118,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Color","Cloud Layer1 Color","Cloud layer 1",AtmosphereType::color,119,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1AlphaMul","Cloud Layer1 Alpha Mul","Cloud layer 1",AtmosphereType::number,120,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Texture","Cloud Layer1 Texture","Cloud layer 1",AtmosphereType::texture,121,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Absorption","Cloud Layer1 Absorption","Cloud layer 1",AtmosphereType::number,122,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Scattering","Cloud Layer1 Scattering","Cloud layer 1",AtmosphereType::number,123,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Phase","Cloud Layer1 Phase","Cloud layer 1",AtmosphereType::number,124,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer1Thickness","Cloud Layer1 Thickness","Cloud layer 1",AtmosphereType::number,125,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Altitude","Cloud Layer2 Altitude","Cloud layer 2",AtmosphereType::number,126,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2TileFactor","Cloud Layer2 Tile Factor","Cloud layer 2",AtmosphereType::number,127,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Rotation","Cloud Layer2 Rotation","Cloud layer 2",AtmosphereType::number,128,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Speed","Cloud Layer2 Speed","Cloud layer 2",AtmosphereType::number,129,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2SunLightIntensity","Cloud Layer2 Sun Light Intensity","Cloud layer 2",AtmosphereType::number,130,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2SunLightPower","Cloud Layer2 Sun Light Power","Cloud layer 2",AtmosphereType::number,131,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2AmbientLightIntensity","Cloud Layer2 Ambient Light Intensity","Cloud layer 2",AtmosphereType::number,132,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Color","Cloud Layer2 Color","Cloud layer 2",AtmosphereType::color,133,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2AlphaMul","Cloud Layer2 Alpha Mul","Cloud layer 2",AtmosphereType::number,134,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Texture","Cloud Layer2 Texture","Cloud layer 2",AtmosphereType::texture,135,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Absorption","Cloud Layer2 Absorption","Cloud layer 2",AtmosphereType::number,136,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Scattering","Cloud Layer2 Scattering","Cloud layer 2",AtmosphereType::number,137,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Phase","Cloud Layer2 Phase","Cloud layer 2",AtmosphereType::number,138,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.CloudLayer2Thickness","Cloud Layer2 Thickness","Cloud layer 2",AtmosphereType::number,139,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.StaticEnvmapTexture","Static Envmap Texture","Environment map",AtmosphereType::texture,140,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.StaticEnvmapScale","Static Envmap Scale","Environment map",AtmosphereType::number,141,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.EarthRadius","Earth Radius","Physical atmosphere",AtmosphereType::number,142,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.AtmosphereRadius","Atmosphere Radius","Physical atmosphere",AtmosphereType::number,143,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.MieScatteringCoefficient","Mie Scattering Coefficient","Physical atmosphere",AtmosphereType::number,144,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.MieG","Mie G","Physical atmosphere",AtmosphereType::number,145,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.MieExtinctionCoefficientRelation","Mie Extinction Coefficient Relation","Physical atmosphere",AtmosphereType::number,146,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ScaleHeightMie","Scale Height Mie","Physical atmosphere",AtmosphereType::number,147,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.RayleighScatteringCoefficient","Rayleigh Scattering Coefficient","Physical atmosphere",AtmosphereType::vector,148,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.RayleighScatteringCoefficientScale","Rayleigh Scattering Coefficient Scale","Physical atmosphere",AtmosphereType::number,149,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.RayleighExtinctionCoefficientRelation","Rayleigh Extinction Coefficient Relation","Physical atmosphere",AtmosphereType::number,150,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ScaleHeightRayleigh","Scale Height Rayleigh","Physical atmosphere",AtmosphereType::number,151,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.UseOzone","Use Ozone","Physical atmosphere",AtmosphereType::toggle,152,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.OzonePercentage","Ozone Percentage","Physical atmosphere",AtmosphereType::number,153,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.UseAerialPerspective","Use Aerial Perspective","Physical atmosphere",AtmosphereType::toggle,154,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.AerialPerspectiveScale","Aerial Perspective Scale","Physical atmosphere",AtmosphereType::number,155,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.AerialPerspectiveIntensity","Aerial Perspective Intensity","Physical atmosphere",AtmosphereType::number,156,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.AerialPerspectiveDithering","Aerial Perspective Dithering","Physical atmosphere",AtmosphereType::number,157,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.AerialPerspectiveMaxDistance","Aerial Perspective Max Distance","Physical atmosphere",AtmosphereType::number,158,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1Color","Light1 Color","Primary light",AtmosphereType::color,159,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1Intensity","Light1 Intensity","Primary light",AtmosphereType::number,160,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1FollowOutdoorLight","Light1 Follow Outdoor Light","Primary light",AtmosphereType::toggle,161,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1TakesColorFromOutdoorLight","Light1 Takes Color From Outdoor Light","Primary light",AtmosphereType::toggle,162,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1RotX","Light1 Rot X","Primary light",AtmosphereType::number,163,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light1RotY","Light1 Rot Y","Primary light",AtmosphereType::number,164,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.UseLightSource2","Use Light Source2","Secondary light",AtmosphereType::toggle,165,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light2Color","Light2 Color","Secondary light",AtmosphereType::color,166,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light2Intensity","Light2 Intensity","Secondary light",AtmosphereType::number,167,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light2RotX","Light2 Rot X","Secondary light",AtmosphereType::number,168,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.Light2RotY","Light2 Rot Y","Secondary light",AtmosphereType::number,169,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.UseNoise","Use Noise","Physical atmosphere",AtmosphereType::toggle,170,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.FogStartDistance","Fog Start Distance","Physical atmosphere",AtmosphereType::number,171,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.RayleighPolarization","Rayleigh Polarization","Physical atmosphere",AtmosphereType::vector,172,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.MiePolarization","Mie Polarization","Physical atmosphere",AtmosphereType::vector,173,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.OutdoorLightScale","Outdoor Light Scale","Physical atmosphere",AtmosphereType::vector,174,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.DrawSunDisc","Draw Sun Disc","Physical atmosphere",AtmosphereType::toggle,175,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ForwardScatteringDepthVisibility","Forward Scattering Depth Visibility","Forward scattering",AtmosphereType::vector,176,0,4,45,0,-1000000000.0,1000000000.0},
    {"Sky.ForwardScatteringStartDepth","Forward Scattering Start Depth","Forward scattering",AtmosphereType::number,177,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ForwardScatteringEndDepth","Forward Scattering End Depth","Forward scattering",AtmosphereType::number,178,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ForwardScatteringTakesColorFromOutdoorLight","Forward Scattering Takes Color From Outdoor Light","Forward scattering",AtmosphereType::number,179,0,1,45,0,-1000000000.0,1000000000.0},
    {"Sky.ForwardScatteringOutdoorLightTint","Forward Scattering Outdoor Light Tint","Forward scattering",AtmosphereType::color,180,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.HeightFogColorAdd","Height Fog Color Add","Height fog",AtmosphereType::color,181,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.HeightFogColorMult","Height Fog Color Mult","Height fog",AtmosphereType::color,182,0,3,45,0,-1000000000.0,1000000000.0},
    {"Sky.MinHeightFogTransmittance","Min Height Fog Transmittance","Height fog",AtmosphereType::number,183,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindDirection","Wind Direction","Wind",AtmosphereType::number,184,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindDirectionVariationMultiplier","Wind Direction Variation Multiplier","Wind",AtmosphereType::number,185,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindStrength","Wind Strength","Wind",AtmosphereType::number,186,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindVariationMultiplier","Wind Variation Multiplier","Wind",AtmosphereType::number,187,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindVariationRateMultiplier","Wind Variation Rate Multiplier","Wind",AtmosphereType::number,188,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindMicroVariationMultiplier","Wind Micro Variation Multiplier","Wind",AtmosphereType::number,189,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindOscillationAmplitude","Wind Oscillation Amplitude","Wind",AtmosphereType::number,190,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.WindOscillationFrequency","Wind Oscillation Frequency","Wind",AtmosphereType::number,191,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.TurbulenceMultiplier","Turbulence Multiplier","Wind",AtmosphereType::number,192,0,1,45,0,-1000000000.0,1000000000.0},
    {"Wind.TurbulenceScale","Turbulence Scale","Wind",AtmosphereType::number,193,0,1,45,0,-1000000000.0,1000000000.0},
}};

struct AtmosphereValue {
    std::array<double,4> number{};
    std::string texture;
    bool operator==(const AtmosphereValue&) const = default;
};
using AtmosphereChoices = std::map<std::string, AtmosphereValue, std::less<>>;
struct AtmosphereReading { std::optional<AtmosphereValue> value; bool varies{}, applied{}; };
inline bool valid_atmosphere_value(const AtmosphereControl& c, const AtmosphereValue& v) {
    if (c.type == AtmosphereType::texture)
        return !v.texture.empty() && v.texture.size() <= 512 && v.texture.find_first_of("\r\n\"") == std::string::npos &&
            std::all_of(v.number.begin(), v.number.end(), [](double n) { return n == 0; });
    if (!v.texture.empty()) return false;
    for (unsigned i=0; i<4; ++i) {
        const double n = v.number[i];
        if (!std::isfinite(n) || (i >= c.lanes && n != 0)) return false;
        if (i >= c.lanes) continue;
        if (c.type == AtmosphereType::toggle) { if (n != 0 && n != 1) return false; }
        else if (c.type == AtmosphereType::enumeration) {
            bool found = false;
            for (unsigned j=0; j<c.option_count; ++j) found |= n == atmosphere_options[c.option_start+j].value;
            if (!found) return false;
        } else {
            if (n < c.minimum || n > c.maximum) return false;
            if (c.type == AtmosphereType::integer && std::floor(n) != n) return false;
        }
    }
    return true;
}
inline const AtmosphereControl* find_atmosphere_control(std::string_view key) {
    for (const auto& c : atmosphere_controls) if (key == c.key) return &c;
    return nullptr;
}
inline bool valid_atmosphere_choices(const AtmosphereChoices& values) {
    for (const auto& [key,v] : values) {
        const auto* c = find_atmosphere_control(key);
        if (!c || !valid_atmosphere_value(*c,v)) return false;
    }
    return true;
}
inline std::string atmosphere_value_text(const AtmosphereControl& c, const AtmosphereValue& v) {
    if (c.type == AtmosphereType::texture) return v.texture;
    std::string text;
    for (unsigned i=0; i<c.lanes; ++i) {
        if (i) text += ',';
        std::array<char,64> buffer{};
        const auto r = std::to_chars(buffer.data(),buffer.data()+buffer.size(),v.number[i],std::chars_format::general,9);
        if (r.ec != std::errc{}) return {};
        text.append(buffer.data(),r.ptr);
    }
    return text;
}
inline bool parse_atmosphere_value(const AtmosphereControl& c, std::string_view text, AtmosphereValue& v) {
    v = {};
    if (c.type == AtmosphereType::texture) v.texture = text;
    else {
        for (unsigned i=0; i<c.lanes; ++i) {
            const auto end = text.find(',');
            const auto token = text.substr(0,end);
            const auto r = std::from_chars(token.data(),token.data()+token.size(),v.number[i]);
            if (r.ec != std::errc{} || r.ptr != token.data()+token.size()) return false;
            if (i+1 == c.lanes) { if (end != std::string_view::npos) return false; }
            else { if (end == std::string_view::npos) return false; text.remove_prefix(end+1); }
        }
    }
    return valid_atmosphere_value(c,v);
}
}

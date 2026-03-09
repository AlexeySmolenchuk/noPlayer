#include "noPlayer.h"


#include <OpenColorIO/OpenColorIO.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace OCIO = OCIO_NAMESPACE;

namespace
{
GLint interpolationToGlFilter(OCIO::Interpolation interpolation)
{
	switch (interpolation)
	{
		case OCIO::INTERP_NEAREST:
			return GL_NEAREST;
		case OCIO::INTERP_LINEAR:
		case OCIO::INTERP_TETRAHEDRAL:
		case OCIO::INTERP_BEST:
		case OCIO::INTERP_DEFAULT:
		default:
			return GL_LINEAR;
	}
}
}

void NoPlayer::configureOCIO()
{
	// Reset previously uploaded LUT textures before rebuilding OCIO state.
	releaseOCIOTextures();

	OCIO::ConstConfigRcPtr config;
	std::string configSource;

	auto loadBuiltinConfig = [&]()
	{
		// Use a deterministic builtin profile when no external config is available.
		const char* builtinName = OCIO::BuiltinConfigRegistry::Get().getBuiltinConfigName(0);
		config = OCIO::Config::CreateFromBuiltinConfig(builtinName);
		configSource = std::string("builtin: ") + builtinName;
	};

	// Prefer OCIO environment config so user/project color management is respected.
	const char* ocioPath = std::getenv("OCIO");
	if (ocioPath && ocioPath[0] != '\0')
	{
		try
		{
			config = OCIO::Config::CreateFromEnv();
			config->validate();
			if (config->getNumDisplays() <= 0)
				throw std::runtime_error("No displays in OCIO config.");
			configSource = std::string("env: ") + ocioPath;
		}
		catch (const std::exception& exception)
		{
			std::cerr << "OCIO: failed to load config from OCIO='" << ocioPath << "' (" << exception.what()
					<< "). Falling back to built-in config.\n";
			loadBuiltinConfig();
		}
	}
	else
	{
		loadBuiltinConfig();
	}

	ocioConfigSource = configSource;

	// Gather available displays from the loaded config.
	ocioDisplays.clear();
	for (int index = 0; index < config->getNumDisplays(); index++)
		ocioDisplays.push_back(config->getDisplay(index));

	auto hasName = [](const std::vector<std::string>& names, const std::string& candidate)
	{
		return std::find(names.begin(), names.end(), candidate) != names.end();
	};

	std::string g_inputColorSpace;
	std::string g_display;
	std::string g_transformName;
	std::string g_look;
	OCIO::OptimizationFlags g_optimization{ OCIO::OPTIMIZATION_DEFAULT };

	// Keep current display when valid, otherwise fall back to config defaults.
	if (!ocioSelectedDisplay.empty() && hasName(ocioDisplays, ocioSelectedDisplay))
		g_display = ocioSelectedDisplay;
	else
	{
		g_display = config->getDefaultDisplay();
		if (!hasName(ocioDisplays, g_display) && !ocioDisplays.empty())
			g_display = ocioDisplays.front();
	}

	// Gather views for the selected display.
	ocioViews.clear();
	for (int index = 0; index < config->getNumViews(g_display.c_str()); index++)
		ocioViews.push_back(config->getView(g_display.c_str(), index));
	if (ocioViews.empty())
	{
		// Fall back to the first display that exposes at least one view.
		for (const std::string& displayName : ocioDisplays)
		{
			ocioViews.clear();
			for (int index = 0; index < config->getNumViews(displayName.c_str()); index++)
				ocioViews.push_back(config->getView(displayName.c_str(), index));
			if (!ocioViews.empty())
			{
				g_display = displayName;
				break;
			}
		}
	}

	// Keep current view when valid, otherwise use display default/first view.
	if (!ocioSelectedView.empty() && hasName(ocioViews, ocioSelectedView))
		g_transformName = ocioSelectedView;
	else
	{
		g_transformName = config->getDefaultView(g_display.c_str());
		if (!hasName(ocioViews, g_transformName) && !ocioViews.empty())
			g_transformName = ocioViews.front();
	}

	ocioSelectedDisplay = g_display;
	ocioSelectedView = g_transformName;

	// Select active display and view from the loaded config.
	g_look = config->getDisplayViewLooks(g_display.c_str(), g_transformName.c_str());
	if (config->hasRole(OCIO::ROLE_SCENE_LINEAR))
		g_inputColorSpace = OCIO::ROLE_SCENE_LINEAR;
	else if (config->getNumColorSpaces() > 0)
		g_inputColorSpace = config->getColorSpaceNameByIndex(0);
	else
		g_inputColorSpace = OCIO::ROLE_SCENE_LINEAR;

	// Print selected OCIO source and display/view for runtime diagnostics.
	std::cout << "OCIO: using " << configSource
			  << ", display='" << g_display
			  << "', view='" << g_transformName << "'\n";



	OCIO::DisplayViewTransformRcPtr transform = OCIO::DisplayViewTransform::Create();
	transform->setSrc( g_inputColorSpace.c_str() );
	transform->setDisplay( g_display.c_str() );
	transform->setView( g_transformName.c_str() );

	OCIO::LegacyViewingPipelineRcPtr vp = OCIO::LegacyViewingPipeline::Create();
	vp->setDisplayViewTransform(transform);
	vp->setLooksOverrideEnabled(true);
	vp->setLooksOverride(g_look.c_str());

	// Build one processor describing scene-linear to display-view conversion.
	OCIO::ConstProcessorRcPtr processor;
	processor = vp->getProcessor(config, config->getCurrentContext());

	OCIO::GpuShaderDescRcPtr shaderDesc = OCIO::GpuShaderDesc::CreateShaderDesc();
	shaderDesc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
	shaderDesc->setFunctionName("OCIODisplay");
	shaderDesc->setResourcePrefix("ocio_");

	// Generate shader text and LUT resources needed for the selected processor.
	OCIO::ConstGPUProcessorRcPtr gpu = processor->getOptimizedGPUProcessor(g_optimization);
	gpu->extractGpuShaderInfo(shaderDesc);

	GLint maxTextureUnits = 0;
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxTextureUnits);
	GLint nextTextureUnit = 1;

	auto acquireTextureUnit = [&]() -> GLint
	{
		// Reserve texture unit 0 for the image texture sampler.
		if (nextTextureUnit >= maxTextureUnits)
			return -1;
		return nextTextureUnit++;
	};

	// Upload all OCIO 1D/2D LUT textures requested by the generated shader.
	for (unsigned int index = 0; index < shaderDesc->getNumTextures(); index++)
	{
		const char* textureName = nullptr;
		const char* samplerName = nullptr;
		unsigned int width = 0;
		unsigned int height = 0;
		OCIO::GpuShaderDesc::TextureType channel;
		OCIO::GpuShaderDesc::TextureDimensions dimensions;
		OCIO::Interpolation interpolation;
		const float* values = nullptr;

		shaderDesc->getTexture(index, textureName, samplerName, width, height, channel, dimensions, interpolation);
		shaderDesc->getTextureValues(index, values);

		const GLint unit = acquireTextureUnit();
		if (unit < 0)
		{
			std::cerr << "OCIO: not enough texture units for LUT '" << (textureName ? textureName : "unknown") << "'.\n";
			break;
		}

		const GLenum target = (dimensions == OCIO::GpuShaderDesc::TEXTURE_1D) ? GL_TEXTURE_1D : GL_TEXTURE_2D;
		const GLenum format = (channel == OCIO::GpuShaderDesc::TEXTURE_RED_CHANNEL) ? GL_RED : GL_RGB;
		const GLint internalFormat = (channel == OCIO::GpuShaderDesc::TEXTURE_RED_CHANNEL) ? GL_R32F : GL_RGB32F;
		const GLint filter = interpolationToGlFilter(interpolation);

		GLuint textureId = 0;
		glGenTextures(1, &textureId);
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(target, textureId);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		if (target == GL_TEXTURE_2D)
			glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		if (target == GL_TEXTURE_1D)
			glTexImage1D(target, 0, internalFormat, width, 0, format, GL_FLOAT, values);
		else
			glTexImage2D(target, 0, internalFormat, width, height, 0, format, GL_FLOAT, values);

		ocioLutTextures.push_back({textureId, target, unit, samplerName ? samplerName : ""});
	}

	// Upload all OCIO 3D LUT textures requested by the generated shader.
	for (unsigned int index = 0; index < shaderDesc->getNum3DTextures(); index++)
	{
		const char* textureName = nullptr;
		const char* samplerName = nullptr;
		unsigned int edgeLen = 0;
		OCIO::Interpolation interpolation;
		const float* values = nullptr;

		shaderDesc->get3DTexture(index, textureName, samplerName, edgeLen, interpolation);
		shaderDesc->get3DTextureValues(index, values);

		const GLint unit = acquireTextureUnit();
		if (unit < 0)
		{
			std::cerr << "OCIO: not enough texture units for 3D LUT '" << (textureName ? textureName : "unknown") << "'.\n";
			break;
		}

		GLuint textureId = 0;
		const GLint filter = interpolationToGlFilter(interpolation);
		glGenTextures(1, &textureId);
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_3D, textureId);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, filter);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, filter);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, edgeLen, edgeLen, edgeLen, 0, GL_RGB, GL_FLOAT, values);

		ocioLutTextures.push_back({textureId, GL_TEXTURE_3D, unit, samplerName ? samplerName : ""});
	}
	// Restore default active texture unit for the main render path.
	glActiveTexture(GL_TEXTURE0);

	// Rebuild fragment shader source with embedded OCIO function code.
	fragmentShaderCode = R"glsl(#version 330 core
		out vec4 FragColor;
		in vec2 texCoords;
		uniform sampler2D textureSampler;
		uniform float gainValues;
		uniform float offsetValues;
		uniform int soloing;
		uniform int soloChannelIndex;
		uniform int nchannels;
		uniform float flash;
		uniform int doOCIO;
		uniform int checkNaN;
	)glsl" +
	std::string(shaderDesc->getShaderText()) +
	R"glsl(
		vec3 rgbToHsv(vec3 rgb)
		{
			float cmax = max(rgb.r, max(rgb.g, rgb.b));
			float cmin = min(rgb.r, min(rgb.g, rgb.b));
			float delta = cmax - cmin;

			float h = 0.0;
			float s = 0.0;
			float v = cmax;

			if (delta > 0.000001)
			{
				if (cmax > 0.000001)
					s = delta / cmax;

				if (cmax == rgb.r)
					h = mod((rgb.g - rgb.b) / delta, 6.0);
				else if (cmax == rgb.g)
					h = ((rgb.b - rgb.r) / delta) + 2.0;
				else
					h = ((rgb.r - rgb.g) / delta) + 4.0;

				h /= 6.0;
				if (h < 0.0)
					h += 1.0;
			}

			return vec3(h, s, v);
		}

		float rgbToY(vec3 rgb)
		{
			return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
		}

		void main() {
			FragColor = vec4(0.0);
			vec4 fragment = texture(textureSampler, texCoords.xy);
			for (int i=0; i<nchannels; i++){
				float f = abs(flash-0.5)*2.0;
				if (isnan(fragment[i]))
				{
					if (checkNaN==1)
					{
						FragColor = vec4(1, 0.25, 0, 0) * f;
						return;
					}
					else
						fragment[i] = 0;
				}
				else if (isinf(fragment[i]))
				{
					if (checkNaN==1)
					{
						FragColor = vec4(1, 0.25, 0, 0) * (1.0-f);
						return;
					}
					else
						fragment[i] = 0;
				}
			}

			vec4 rawFragment = fragment;

			fragment += vec4(offsetValues);
			fragment *= gainValues;

			FragColor = fragment;

			if (nchannels==1)
			{
				FragColor = FragColor.rrrr;
				return;
			}

			if (doOCIO==1)
				FragColor = OCIODisplay(fragment);

			if (soloing!=0){
				if ((soloing == 1 || soloing == 2 || soloing == 3 || soloing == 8)
					&& soloChannelIndex >= 0
					&& soloChannelIndex < nchannels)
				{
					FragColor = vec4(FragColor[soloChannelIndex]);
				}
				else
				{
					switch (soloing) {
					case 4:
					{
						vec3 hsv = rgbToHsv(rawFragment.rgb);
						FragColor = vec4(hsv.xxx, FragColor.a);
						break;
					}
					case 5:
					{
						vec3 hsv = rgbToHsv(rawFragment.rgb);
						FragColor = vec4(hsv.yyy, FragColor.a);
						break;
					}
					case 6:
					{
						vec3 hsv = rgbToHsv(rawFragment.rgb);
						FragColor = vec4(hsv.zzz, FragColor.a);
						break;
					}
					case 7:
					{
						float y = rgbToY(rawFragment.rgb);
						FragColor = vec4(vec3(y), FragColor.a);
						break;
					}
					}
				}
			}
		}
	)glsl";


}

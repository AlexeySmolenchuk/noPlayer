#include "noPlayer.h"


#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;

void NoPlayer::configureOCIO()
{
	// std::cout << std::getenv( "OCIO" ) << std::endl;

	// for (int i = 0; i < OCIO::BuiltinConfigRegistry::Get().getNumBuiltinConfigs(); i++)
	// {
	// 	std::cout <<
	// 	OCIO::BuiltinConfigRegistry::Get().getBuiltinConfigName(i)
	// 	<< std::endl;
	// }

	OCIO::ConstConfigRcPtr config;

	// if(OCIO::IsEnvVariablePresent("OCIO"))
	// 	config = OCIO::GetCurrentConfig();
	// else
		config = OCIO::Config::CreateFromBuiltinConfig(OCIO::BuiltinConfigRegistry::Get().getBuiltinConfigName(0));


	std::string g_inputColorSpace;
	std::string g_display;
	std::string g_transformName;
	std::string g_look;
	OCIO::OptimizationFlags g_optimization{ OCIO::OPTIMIZATION_DEFAULT }; //OPTIMIZATION_DRAFT

	g_display = config->getDefaultDisplay();
	g_transformName = config->getDefaultView(g_display.c_str());

	// std::cout << "ActiveViews: " << config->getActiveViews() << std::endl;

	g_look = config->getDisplayViewLooks(g_display.c_str(), g_transformName.c_str());
	g_inputColorSpace = OCIO::ROLE_SCENE_LINEAR;

	// std::cout << "g_display " << g_display << std::endl;
	// std::cout << "g_transformName " <<  g_transformName << std::endl;
	// std::cout << "g_look " << g_look << std::endl;

	// for (int i = 0; i < config->getNumViews(g_display.c_str()); i++)
	// {
	// 	std::cout <<
	// 	config->getView(g_display.c_str(), i)
	// 	<< std::endl;
	// }

	OCIO::DisplayViewTransformRcPtr transform = OCIO::DisplayViewTransform::Create();
	transform->setSrc( g_inputColorSpace.c_str() );
	transform->setDisplay( g_display.c_str() );
	transform->setView( g_transformName.c_str() );

	OCIO::LegacyViewingPipelineRcPtr vp = OCIO::LegacyViewingPipeline::Create();
	vp->setDisplayViewTransform(transform);
	vp->setLooksOverrideEnabled(true);
	vp->setLooksOverride(g_look.c_str());

	// auto ect = OCIO::ExposureContrastTransform::Create();
	// ect->setStyle(OCIO::EXPOSURE_CONTRAST_LOGARITHMIC);
	// ect->makeExposureDynamic();
	// vp->setLinearCC(ect);

	OCIO::ConstProcessorRcPtr processor;
	processor = vp->getProcessor(config, config->getCurrentContext());

	// Set the shader context.
	OCIO::GpuShaderDescRcPtr shaderDesc = OCIO::GpuShaderDesc::CreateShaderDesc();
	shaderDesc->setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_3);
	shaderDesc->setFunctionName("OCIODisplay");
	shaderDesc->setResourcePrefix("ocio_");

	// Extract the shader information.
	OCIO::ConstGPUProcessorRcPtr gpu = processor->getOptimizedGPUProcessor(g_optimization);
	gpu->extractGpuShaderInfo(shaderDesc);

	fragmentShaderCode = R"glsl(#version 330 core
		out vec4 FragColor;
		in vec2 texCoords;
		uniform sampler2D textureSampler;
		uniform float gainValues;
		uniform float offsetValues;
		uniform int soloing;
		uniform int nchannels;
		uniform float flash;
		uniform int doOCIO;
		uniform int checkNaN;
	)glsl" +
	std::string(shaderDesc->getShaderText()) +
	R"glsl(
		vec3 rgbToHsl(vec3 rgb)
		{
			float cmax = max(rgb.r, max(rgb.g, rgb.b));
			float cmin = min(rgb.r, min(rgb.g, rgb.b));
			float delta = cmax - cmin;

			float h = 0.0;
			float s = 0.0;
			float l = (cmax + cmin) * 0.5;

			if (delta > 0.000001)
			{
				float denominator = 1.0 - abs(2.0 * l - 1.0);
				if (abs(denominator) > 0.000001)
					s = delta / denominator;
				else
					s = 0.0;

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

			return vec3(h, s, l);
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
				switch (soloing) {
				case 1:
					FragColor = FragColor.rrrr;
					break;
				case 2:
					FragColor = FragColor.gggg;
					break;
				case 3:
					FragColor = FragColor.bbbb;
					break;
				case 4:
				{
					vec3 hsl = rgbToHsl(rawFragment.rgb);
					FragColor = vec4(hsl.xxx, FragColor.a);
					break;
				}
				case 5:
				{
					vec3 hsl = rgbToHsl(rawFragment.rgb);
					FragColor = vec4(hsl.yyy, FragColor.a);
					break;
				}
				case 6:
				{
					vec3 hsl = rgbToHsl(rawFragment.rgb);
					FragColor = vec4(hsl.zzz, FragColor.a);
					break;
				}
				case 7:
					FragColor = FragColor.aaaa;
					break;
				}
			}
		}
	)glsl";

	// std::cout << shaderDesc->getShaderText() << std::endl;

	// std::cout << "Num3DTextures:  " << shaderDesc->getNum3DTextures() << std::endl;
	// std::cout << "tNumTextures:" << shaderDesc->getNumTextures() << std::endl;
}

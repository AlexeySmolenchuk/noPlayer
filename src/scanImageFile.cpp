#include "noPlayer.h"

#include <unordered_map>
#include <utility>

namespace
{
void initializePlaneFromSpec(ImagePlaneData& plane,
							 const std::string& imageFileName,
							 unsigned int subimage,
							 unsigned int mip,
							 const OIIO::ImageSpec& spec,
							 const std::string& planeName,
							 const std::string& groupName,
							 int begin,
							 bool windowMatchData,
							 float pixelAspect,
							 const std::pair<std::string, int>& compressionInfo,
							 const std::shared_ptr<OIIO::ImageCache>& cache)
{
	plane.imageFileName = imageFileName;
	plane.subimage = subimage;
	plane.mip = mip;
	plane.name = planeName;
	plane.groupName = groupName;
	plane.format = spec.channelformat(begin).c_str();
	plane.begin = begin;
	plane.len = 0;

	plane.imageWidth = spec.width;
	plane.imageHeight = spec.height;
	plane.imageOffsetX = spec.x;
	plane.imageOffsetY = spec.y;
	plane.windowWidth = spec.full_width;
	plane.windowHeight = spec.full_height;
	plane.windowOffsetX = spec.full_x;
	plane.windowOffsetY = spec.full_y;

	plane.windowMatchData = windowMatchData;
	plane.pixelAspect = pixelAspect;
	plane.compression = compressionInfo.first;
	plane.quality = compressionInfo.second;
	plane.tile_width = spec.tile_width;
	plane.tile_height = spec.tile_height;
	plane.cache = cache;
}
}


bool NoPlayer::scanImageFile()
{
	// Reset counters before scanning a new file.
	subimages = 0;
	mips = 0;

	using namespace OIIO;

	auto inp = ImageInput::open (imageFileName);
	if (! inp)
	{
		message = OIIO::geterror();
		std::cerr << "Could not open " << imageFileName
				<< ", error = " << message << "\n";
		return false;
	}

	const std::vector<std::string> predefined = {"RGBA", "XYZ", "UV", "rgba", "xyz", "uv"};
	// Build a flat list first, then regroup into logical AOV planes.
	std::vector<ImagePlaneData> imagePlanesFlattened;
	bool truncatedChannels = false;
	
	int mip = 0;
	while (inp->seek_subimage(subimages, mip))
	{
		// Iterate all mips for the current subimage.
		while (inp->seek_subimage(subimages, mip))
		{
			const ImageSpec &spec = inp->spec();
			const std::string planeName = spec["Name"].get<std::string>();
			const float pixelAspect = spec.get_float_attribute("PixelAspectRatio", 1.0f);
			const std::pair<std::string, int> compressionInfo = spec.decode_compression_metadata();

			bool windowMatchData = (spec.width == spec.full_width &&
									spec.height == spec.full_height &&
									spec.x == spec.full_x &&
									spec.y == spec.full_y);

			std::vector<int> predefinedPlaneIndices(predefined.size(), -1);
			std::unordered_map<std::string, size_t> groupedPlaneIndices;

			for(int i = 0; i < spec.nchannels; i++)
			{
				auto name = spec.channel_name(i);
				size_t pos = name.find_last_of('.');

				if (pos == std::string::npos)
				{
					// Group non-dotted channels using common predefined packs.
					bool groupFound = false;

					for (int n = 0; n < predefined.size(); n++)
					{
						if (predefined[n].find(name) != std::string::npos)
						{
							int planeIndex = predefinedPlaneIndices[n];
							if (planeIndex < 0)
							{
								planeIndex = static_cast<int>(imagePlanesFlattened.size());
								predefinedPlaneIndices[n] = planeIndex;
								ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
								initializePlaneFromSpec(plane, imageFileName, subimages, mip, spec,
														planeName, "", i, windowMatchData,
														pixelAspect, compressionInfo, cache);
							}

							ImagePlaneData &plane = imagePlanesFlattened[static_cast<size_t>(planeIndex)];
							plane.channels += name;
							plane.len++;
							groupFound = true;
							break;
						}
					}

					if (!groupFound)
					{
						// Fallback to one-channel logical planes when no group matches.
						ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
						initializePlaneFromSpec(plane, imageFileName, subimages, mip, spec,
												planeName, "", i, windowMatchData,
												pixelAspect, compressionInfo, cache);
						plane.channels += name;
						plane.len = 1;
					}
				}
				else
				{
					// Group dotted channels by their group prefix.
					auto channel_group = name.substr(0, pos);
					size_t planeIndex = imagePlanesFlattened.size();
					auto groupIt = groupedPlaneIndices.find(channel_group);
					if (groupIt == groupedPlaneIndices.end())
					{
						ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
						initializePlaneFromSpec(plane, imageFileName, subimages, mip, spec,
												planeName, channel_group, i, windowMatchData,
												pixelAspect, compressionInfo, cache);
						groupedPlaneIndices.emplace(channel_group, planeIndex);
					}
					else
					{
						planeIndex = groupIt->second;
					}

					ImagePlaneData &plane = imagePlanesFlattened[planeIndex];
					plane.len++;
					plane.channels += name.substr(pos+1);
				}
			}
			mip++;
			mips++;
		}
		subimages++;
		mip = 0;
	}

	for (ImagePlaneData &planeData : imagePlanesFlattened)
	{
		// Limit rendering to the first 4 channels per plane.
		if (planeData.len > 4)
		{
			planeData.len = 4;
			if (planeData.channels.size() > 4)
				planeData.channels = planeData.channels.substr(0, 4);
			truncatedChannels = true;
		}
	}

	if (truncatedChannels)
	{
		// Surface channel truncation to the startup message panel.
		if (!message.empty())
			message += "\n";
		message += "Some channel groups have more than 4 channels. Display is limited to the first 4 channels.";
	}

	// Collapse flattened entries into logical AOV planes keyed by name/group/channels.
	std::unordered_map<std::string, size_t> map;

	for (ImagePlaneData& planeData : imagePlanesFlattened)
	{
		std::string key;

		if (!planeData.name.empty())
			key += planeData.name + " ";

		if (!planeData.groupName.empty())
			key += planeData.groupName + " ";

		if (!planeData.channels.empty())
			key += planeData.channels;

		size_t idx = imagePlanes.size();

		if (map.find(key) == map.end())
		{
			map[key] = idx;
			ImagePlane &plane = imagePlanes.emplace_back();
			plane.name = planeData.name;
			plane.groupName = planeData.groupName;
			plane.channels = planeData.channels;
			plane.nMIPs = 0;

			// Enable OCIO by default for float/half RGB-family planes.
			if ((planeData.channels == "RGB") || (planeData.channels == "RGBA") || (planeData.channels == "rgb") || (planeData.channels == "rgba"))
				if ((planeData.format == "half") || (planeData.format == "float"))
					plane.doOCIO = true;

		}
		else
			idx = map[key];

		imagePlanes[idx].MIPs.emplace_back(std::move(planeData));
		imagePlanes[idx].nMIPs++;
	}

	return true;
}

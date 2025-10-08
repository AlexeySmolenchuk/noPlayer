#include "noPlayer.h"


bool NoPlayer::scanImageFile()
{
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

	std::vector<std::string> predefined = {"RGBA", "XYZ", "UV", "rgba", "xyz", "uv"};
	std::vector<ImagePlaneData> imagePlanesFlattened; // Flattened data will be organized by names after reading

	int mip = 0;
	while (inp->seek_subimage(subimages, mip))
	{
		while (inp->seek_subimage(subimages, mip))
		{
			int count = 0;
			const ImageSpec &spec = inp->spec();

			bool windowMatchData = (spec.width == spec.full_width &&
									spec.height == spec.full_height &&
									spec.x == spec.full_x &&
									spec.y == spec.full_y);

			std::vector<bool> isCreated(predefined.size());

			for(int i = 0; i < spec.nchannels; i++)
			{
				auto name = spec.channel_name(i);
				size_t pos = name.find_last_of('.');

				// Grouping for non "dot" separated names
				if (pos == std::string::npos)
				{
					bool groupFound = false;

					// loop through predefined names
					for (int n = 0; n < predefined.size(); n++)
					{
						// Search if current channel e.g. "G" is part of predefined "RGBA"
						if (predefined[n].find(name) != std::string::npos)
						{
							if (!isCreated[n])
							{
								ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
								plane.imageFileName = imageFileName;
								plane.subimage = subimages;
								plane.mip = mip;
								plane.name = spec["Name"].get<std::string>();
								plane.groupName = "";

								plane.format = spec.channelformat(i).c_str();
								plane.begin = i;
								plane.len = 0;

								plane.imageWidth = spec.width;
								plane.imageHeight = spec.height;
								plane.imageOffsetX = spec.x;
								plane.imageOffsetY = spec.y;
								plane.windowWidth = spec.full_width;
								plane.windowHeight = spec.full_height;
								plane.windowOffsetX = spec.full_x;
								plane.windowOffsetY = spec.full_y;

								isCreated[n] = true;
							}

							// Fill additional information for already created plane
							imagePlanesFlattened.back().channels += name;
							imagePlanesFlattened.back().len++;
							groupFound = true;
							break;
						}
					}

					// can't group automatically
					if (!groupFound)
					{
						ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
						plane.imageFileName = imageFileName;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = "";

						plane.channels += name;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 1;

						plane.imageWidth = spec.width;
						plane.imageHeight = spec.height;
						plane.imageOffsetX = spec.x;
						plane.imageOffsetY = spec.y;
						plane.windowWidth = spec.full_width;
						plane.windowHeight = spec.full_height;
						plane.windowOffsetX = spec.full_x;
						plane.windowOffsetY = spec.full_y;
					}
				}
				else // Grouping for "dot" separated names
				{
					auto channel_group = name.substr(0, pos);
					if ((count == 0) || (imagePlanesFlattened.back().groupName != channel_group))
					{
						count++;
						ImagePlaneData &plane = imagePlanesFlattened.emplace_back();
						plane.imageFileName = imageFileName;
						plane.subimage = subimages;
						plane.mip = mip;
						plane.name = spec["Name"].get<std::string>();
						plane.groupName = channel_group;
						plane.format = spec.channelformat(i).c_str();
						plane.begin = i;
						plane.len = 0;

						plane.imageWidth = spec.width;
						plane.imageHeight = spec.height;
						plane.imageOffsetX = spec.x;
						plane.imageOffsetY = spec.y;
						plane.windowWidth = spec.full_width;
						plane.windowHeight = spec.full_height;
						plane.windowOffsetX = spec.full_x;
						plane.windowOffsetY = spec.full_y;
					}
					imagePlanesFlattened.back().len++;
					imagePlanesFlattened.back().channels += name.substr(pos+1);
				}

				ImagePlaneData &plane = imagePlanesFlattened.back();
				plane.windowMatchData = windowMatchData;
				plane.pixelAspect = spec.get_float_attribute("PixelAspectRatio", 1.0);
				plane.compression = spec.decode_compression_metadata().first;
				plane.quality = spec.decode_compression_metadata().second;
				plane.tile_width = spec.tile_width;
				plane.tile_height = spec.tile_height;
				plane.cache = cache;
			}
			mip++;
			mips++;
		}
		subimages++;
		mip = 0;
	}

	// Store all image planes to the name based structure
	std::unordered_map<std::string, size_t> map;

	for (ImagePlaneData planeData: imagePlanesFlattened)
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
			ImagePlane plane;
			plane.name = planeData.name;
			plane.groupName = planeData.groupName;
			plane.channels = planeData.channels;
			plane.nMIPs = 0;

			// By default apply OCIO only for non integer RGB (not xyz) channels
			if ((planeData.channels == "RGB") || (planeData.channels == "RGBA") || (planeData.channels == "rgb") || (planeData.channels == "rgba"))
				if ((planeData.format == "half") || (planeData.format == "float"))
					plane.doOCIO = true;

			imagePlanes.push_back(plane);
		}
		else
			idx = map[key];

		imagePlanes[idx].MIPs.push_back(planeData);
		imagePlanes[idx].nMIPs++;
	}


	// for (auto plane: imagePlanesFlattened)
	// {
	// 	std::cout <<
	// 	plane.subimage << " " <<
	// 	plane.mip << " " <<
	// 	plane.name << " " <<
	// 	plane.groupName << " " <<
	// 	plane.channels << " " <<
	// 	plane.format << " " <<
	// 	plane.begin << " " <<
	// 	plane.len << std::endl;
	// }

	// std::cout << "Subimages: " << subimage << std::endl;
	// std::cout << "MIPs: " << mips << std::endl;

	// std::cout << spec.serialize(ImageSpec::SerialText) << std::endl;

	return true;
}

#pragma once

#include <GL/glew.h>

struct ImageShaderUniforms
{
	GLint textureSampler = -1;
	GLint offset = -1;
	GLint scale = -1;
	GLint gainValues = -1;
	GLint offsetValues = -1;
	GLint soloing = -1;
	GLint nchannels = -1;
	GLint doOCIO = -1;
	GLint checkNaN = -1;
	GLint flash = -1;

	void cache(GLuint program)
	{
		textureSampler = glGetUniformLocation(program, "textureSampler");
		offset = glGetUniformLocation(program, "offset");
		scale = glGetUniformLocation(program, "scale");
		gainValues = glGetUniformLocation(program, "gainValues");
		offsetValues = glGetUniformLocation(program, "offsetValues");
		soloing = glGetUniformLocation(program, "soloing");
		nchannels = glGetUniformLocation(program, "nchannels");
		doOCIO = glGetUniformLocation(program, "doOCIO");
		checkNaN = glGetUniformLocation(program, "checkNaN");
		flash = glGetUniformLocation(program, "flash");
	}
};

struct FrameShaderUniforms
{
	GLint offset = -1;
	GLint scale = -1;

	void cache(GLuint program)
	{
		offset = glGetUniformLocation(program, "offset");
		scale = glGetUniformLocation(program, "scale");
	}
};

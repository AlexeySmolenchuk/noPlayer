#include "noPlayer.h"


void NoPlayer::createPlane()
{
	// Define a unit quad used by both image and frame render passes.
	GLfloat vertices[] = {
		0.0f, 0.0f,
		0.0f, 1.0f,
		1.0f, 1.0f,
		1.0f, 0.0f,
	};

	glGenVertexArrays(1, &VAO);
	glBindVertexArray(VAO);

	glGenBuffers(1, &VBO);
	glBindBuffer(GL_ARRAY_BUFFER, VBO);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);

	// Leave a clean GL state after VAO/VBO setup.
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

}


void NoPlayer::addShader(GLuint program, const char* shader_code, GLenum type)
{
	// Compile one shader stage from source text.
	GLuint current_shader = glCreateShader(type);

	const GLchar* code[1];
	code[0] = shader_code;

	GLint code_length[1];
	code_length[0] = (GLint)strlen(shader_code);

	glShaderSource(current_shader, 1, code, code_length);
	glCompileShader(current_shader);

	GLint result = 0;
	GLchar log[1024] = {0};

	glGetShaderiv(current_shader, GL_COMPILE_STATUS, &result);
	if (!result)
	{
		glGetShaderInfoLog(current_shader, sizeof(log), NULL, log);
		std::cout << "Error compiling " << type << " shader: " << log << "\n";
		glDeleteShader(current_shader);
		return;
	}

	glAttachShader(program, current_shader);
	// Shader object can be deleted after being attached to the program.
	glDeleteShader(current_shader);
}


void NoPlayer::createShaders()
{
	// Build the main image program with OCIO-aware fragment code.
	shader = glCreateProgram();
	if(!shader)
	{
		std::cout << "Error creating shader program!\n";
		exit(1);
	}

	addShader(shader, vertexShaderCode.c_str(), GL_VERTEX_SHADER);
	addShader(shader, fragmentShaderCode.c_str(), GL_FRAGMENT_SHADER);

	GLint result = 0;
	GLchar log[1024] = {0};

	glLinkProgram(shader);
	glGetProgramiv(shader, GL_LINK_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(log), NULL, log);
		std::cout << "Error linking program:\n" << log << '\n';
		return;
	}

	glUseProgram(shader);
	{
		// Bind the image sampler to texture unit 0.
		GLint location = glGetUniformLocation(shader, "textureSampler");
		if (location >= 0)
			glUniform1i(location, 0);

		for (const OcioLutTexture& texture : ocioLutTextures)
		{
			if (texture.samplerName.empty())
				continue;
			location = glGetUniformLocation(shader, texture.samplerName.c_str());
			if (location >= 0)
				glUniform1i(location, texture.unit);
		}
	}
	glUseProgram(0);

	// Validate after sampler units are assigned to avoid sampler-type conflicts.
	glValidateProgram(shader);
	glGetProgramiv(shader, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(shader, sizeof(log), NULL, log);
		std::cout << "Warning: shader program validation failed:\n" << log << '\n';
	}

	// Build the frame-outline program used for display-window guides.
	frameShader = glCreateProgram();
	if(!frameShader)
	{
		std::cout << "Error creating shader program!\n";
		exit(1);
	}

	addShader(frameShader, vertexShaderCode.c_str(), GL_VERTEX_SHADER);
	addShader(frameShader, frameFragmentShaderCode.c_str(), GL_FRAGMENT_SHADER);

	glLinkProgram(frameShader);
	glGetProgramiv(frameShader, GL_LINK_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(frameShader, sizeof(log), NULL, log);
		std::cout << "Error linking program:\n" << log << '\n';
		return;
	}

	glValidateProgram(frameShader);
	glGetProgramiv(frameShader, GL_VALIDATE_STATUS, &result);
	if (!result)
	{
		glGetProgramInfoLog(frameShader, sizeof(log), NULL, log);
		std::cout << "Error validating program:\n" << log << '\n';
		return;
	}
}

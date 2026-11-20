#include "GridRenderer.h"

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <vector>
#include <iostream>

static const char* s_VS =
"#version 330 core\n"
"layout(location = 0) in vec3 aPos;\n"
"uniform mat4 u_View;\n"
"uniform mat4 u_Projection;\n"
"uniform float u_NdcBias;\n"
"out vec3 v_WorldPos;\n"
"void main() {\n"
"    v_WorldPos = aPos;\n"
"    vec4 pos = u_Projection * u_View * vec4(aPos, 1.0);\n"
"    pos.z += u_NdcBias * pos.w;\n"
"    gl_Position = pos;\n"
"}\n";

static const char* s_FS =
"#version 330 core\n"
"precision highp float;\n"
"uniform vec3 u_Color;\n"
"uniform vec3 u_CameraPos;\n"
"uniform float u_FadeStart;\n"
"uniform float u_FadeEnd;\n"
"in vec3 v_WorldPos;\n"
"out vec4 FragColor;\n"
"void main() {\n"
"    vec2 dxz = v_WorldPos.xz - u_CameraPos.xz;\n"
"    float distXZ = length(dxz);\n"
"    float alpha = 1.0 - smoothstep(u_FadeStart, u_FadeEnd, distXZ);\n"
"    if (alpha <= 0.001) discard;\n"
"    FragColor = vec4(u_Color, alpha);\n"
"}\n";

GridRenderer::GridRenderer()
{

}

GridRenderer::~GridRenderer()
{
	Destroy();
}

static GLuint CompileShader(GLenum type, const char* src)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, nullptr);
	glCompileShader(shader);
	GLint status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE) {
		GLint len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
		std::string log(len, '\0');
		glGetShaderInfoLog(shader, len, &len, &log[0]);
		std::cerr << "Shader compile error: " << log << std::endl;
	}
	return shader;
}

bool GridRenderer::Create()
{
	GLuint vs = CompileShader(GL_VERTEX_SHADER, s_VS);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, s_FS);

	m_Program = glCreateProgram();
	glAttachShader(m_Program, vs);
	glAttachShader(m_Program, fs);
	glLinkProgram(m_Program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	// Mantemos um VBO separado para linhas menores e maiores. A geometria é
	// reconstruída apenas quando a câmera cruza uma célula do grid.
	glGenVertexArrays(1, &m_VAO);
	glGenBuffers(1, &m_MinorVBO);
	glGenBuffers(1, &m_MajorVBO);
	glGenBuffers(1, &m_AxisVBO);
	glBindVertexArray(m_VAO);
	glBindBuffer(GL_ARRAY_BUFFER, m_MinorVBO);
	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glBindVertexArray(0);

	glBindBuffer(GL_ARRAY_BUFFER, m_AxisVBO);
	glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glUseProgram(m_Program);
	m_locView        = glGetUniformLocation(m_Program, "u_View");
	m_locProjection  = glGetUniformLocation(m_Program, "u_Projection");
	m_locColor       = glGetUniformLocation(m_Program, "u_Color");
	m_locNdcBias     = glGetUniformLocation(m_Program, "u_NdcBias");
	m_locCameraPos   = glGetUniformLocation(m_Program, "u_CameraPos");
	m_locFadeStart   = glGetUniformLocation(m_Program, "u_FadeStart");
	m_locFadeEnd     = glGetUniformLocation(m_Program, "u_FadeEnd");
	m_locGridSpacing = glGetUniformLocation(m_Program, "u_GridSpacing");
	m_locLineWidth = glGetUniformLocation(m_Program, "u_LineWidth");
	m_locMajorStep = glGetUniformLocation(m_Program, "u_MajorStep");
	m_locGridColorMinor = glGetUniformLocation(m_Program, "u_GridColorMinor");
	m_locGridColorMajor = glGetUniformLocation(m_Program, "u_GridColorMajor");
	m_locAxisXColor = glGetUniformLocation(m_Program, "u_AxisXColor");
	m_locAxisYColor = glGetUniformLocation(m_Program, "u_AxisYColor");
	m_locAxisZColor = glGetUniformLocation(m_Program, "u_AxisZColor");

	return true;
}

void GridRenderer::Destroy()
{
	if (m_MinorVBO) { glDeleteBuffers(1, &m_MinorVBO); m_MinorVBO = 0; }
	if (m_MajorVBO) { glDeleteBuffers(1, &m_MajorVBO); m_MajorVBO = 0; }
	if (m_AxisVBO) { glDeleteBuffers(1, &m_AxisVBO); m_AxisVBO = 0; }
	if (m_VAO) { glDeleteVertexArrays(1, &m_VAO); m_VAO = 0; }
	if (m_Program) { glDeleteProgram(m_Program); m_Program = 0; }
	m_GridCacheValid = false;
	m_AxisCacheValid = false;
	m_AxisSpan = 0.0f;
	m_MinorVertices.clear();
	m_MajorVertices.clear();
}

void GridRenderer::Render(const glm::mat4& view, const glm::mat4& projection,
	const glm::vec3& camPos, bool showAxis, bool showGrid, float gridRenderDistance)
{
	if (m_Program == 0)
		return;
	glUseProgram(m_Program);

	const float gridRadius = (gridRenderDistance > 1.0f) ? gridRenderDistance : 500.0f;
	const float fadeStart  = gridRadius * 0.6f;
	const float fadeEnd    = gridRadius;

	if (m_locView >= 0)       glUniformMatrix4fv(m_locView,       1, GL_FALSE, glm::value_ptr(view));
	if (m_locProjection >= 0) glUniformMatrix4fv(m_locProjection, 1, GL_FALSE, glm::value_ptr(projection));
	if (m_locCameraPos >= 0)  glUniform3fv(m_locCameraPos, 1, glm::value_ptr(camPos));
	if (m_locFadeStart >= 0)  glUniform1f(m_locFadeStart, fadeStart);
	if (m_locFadeEnd >= 0)    glUniform1f(m_locFadeEnd,   fadeEnd);

	// -------------------------------------------------------------------
	// Grid LOD + cache
	// -------------------------------------------------------------------
	// Grid com resolução alta constante (1 unidade) em toda a área
	const int centerXi = static_cast<int>(std::round(camPos.x));
	const int centerZi = static_cast<int>(std::round(camPos.z));
	if (!m_GridCacheValid || centerXi != m_LastCenterXi || centerZi != m_LastCenterZi ||
		std::abs(gridRadius - m_LastGridRadius) > 0.5f)
	{
		m_LastCenterXi = centerXi;
		m_LastCenterZi = centerZi;
		m_MinorVertices.clear();
		m_MajorVertices.clear();

		const float spacing = 1.0f;
		const int majorStep = 10;
		const float centerX = static_cast<float>(centerXi);
		const float centerZ = static_cast<float>(centerZi);

		auto addLine = [](std::vector<float>& target,
			float x1, float z1, float x2, float z2)
		{
			target.push_back(x1); target.push_back(0.0f); target.push_back(z1);
			target.push_back(x2); target.push_back(0.0f); target.push_back(z2);
		};

		// Grid com resolução constante de 1 unidade em toda a área
		const int half = static_cast<int>(gridRadius / spacing);
		for (int i = -half; i <= half; ++i)
		{
			const float x = centerX + i * spacing;
			if (std::abs(x) >= spacing * 0.5f)
			{
				const bool major = (((centerXi + i) % majorStep) + majorStep) % majorStep == 0;
				addLine(major ? m_MajorVertices : m_MinorVertices,
					x, centerZ - gridRadius, x, centerZ + gridRadius);
			}

			const float z = centerZ + i * spacing;
			if (std::abs(z) >= spacing * 0.5f)
			{
				const bool major = (((centerZi + i) % majorStep) + majorStep) % majorStep == 0;
				addLine(major ? m_MajorVertices : m_MinorVertices,
					centerX - gridRadius, z, centerX + gridRadius, z);
			}
		}

		m_MinorVertexCount = static_cast<GLsizei>(m_MinorVertices.size() / 3);
		m_MajorVertexCount = static_cast<GLsizei>(m_MajorVertices.size() / 3);

		glBindBuffer(GL_ARRAY_BUFFER, m_MinorVBO);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(m_MinorVertices.size() * sizeof(float)),
			m_MinorVertices.data(), GL_DYNAMIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, m_MajorVBO);
		glBufferData(GL_ARRAY_BUFFER,
			static_cast<GLsizeiptr>(m_MajorVertices.size() * sizeof(float)),
			m_MajorVertices.data(), GL_DYNAMIC_DRAW);
		m_GridCacheValid = true;
		m_LastGridRadius = gridRadius;
	}

	const glm::vec3 minorColor(0.50f, 0.50f, 0.50f);
	const glm::vec3 majorColor(0.68f, 0.68f, 0.68f);

	// -------------------------------------------------------------------
	// Salva estado GL
	// -------------------------------------------------------------------
	GLboolean prevDepthTest; glGetBooleanv(GL_DEPTH_TEST,     &prevDepthTest);
	GLboolean prevBlend;     glGetBooleanv(GL_BLEND,          &prevBlend);
	GLboolean prevDepthMask; glGetBooleanv(GL_DEPTH_WRITEMASK,&prevDepthMask);
	GLint     prevDepthFunc; glGetIntegerv(GL_DEPTH_FUNC,     &prevDepthFunc);
	GLint     prevProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
	GLint     prevVAO = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
	GLint     prevArrayBuffer = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
	GLint     prevActiveTexture = GL_TEXTURE0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
	GLint     prevBlendSrc = GL_ONE; glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
	GLint     prevBlendDst = GL_ZERO; glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
	GLint     prevPolygonMode[2] = { GL_FILL, GL_FILL };
	glGetIntegerv(GL_POLYGON_MODE, prevPolygonMode);
	GLfloat   prevLineWidth = 1.0f; glGetFloatv(GL_LINE_WIDTH, &prevLineWidth);

	glBindVertexArray(m_VAO);

	// -------------------------------------------------------------------
	// 1. GRID — depth test normal, bias NDC = 0
	// -------------------------------------------------------------------
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Sem bias para o grid
	if (m_locNdcBias >= 0) glUniform1f(m_locNdcBias, 0.0f);

	if (showGrid && m_MinorVertexCount > 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_MinorVBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		if (m_locColor >= 0) glUniform3fv(m_locColor, 1, glm::value_ptr(minorColor));
		glLineWidth(1.0f);
		glDrawArrays(GL_LINES, 0, m_MinorVertexCount);
	}

	if (showGrid && m_MajorVertexCount > 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, m_MajorVBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		if (m_locColor >= 0) glUniform3fv(m_locColor, 1, glm::value_ptr(majorColor));
		glLineWidth(2.0f);
		glDrawArrays(GL_LINES, 0, m_MajorVertexCount);
	}

	// Eixos 3D no mundo: desenhados apenas no trecho que cai dentro da área
	// do grid (raio em volta da câmera). Se a origem estiver fora da área,
	// os eixos simplesmente não aparecem ou aparecem só o trecho visível.
	if (showAxis && m_AxisVBO != 0)
	{
		const int centerXi = static_cast<int>(std::round(camPos.x));
		const int centerZi = static_cast<int>(std::round(camPos.z));
		const float centerX = static_cast<float>(centerXi);
		const float centerZ = static_cast<float>(centerZi);

		// Regenera quando a câmera cruza uma célula ou a distância do grid muda
		if (!m_AxisCacheValid ||
			centerXi != m_LastAxisCenterXi ||
			centerZi != m_LastAxisCenterZi ||
			std::abs(gridRadius - m_LastAxisGridRadius) > 0.5f)
		{
			m_LastAxisCenterXi = centerXi;
			m_LastAxisCenterZi = centerZi;
			m_LastAxisGridRadius = gridRadius;

			std::vector<float> axisVertices;
			axisVertices.reserve(6 * 3); // até 3 eixos = 6 vértices

			// --- Eixo X (y=0, z=0) ---
			// Aparece apenas se z=0 estiver dentro da faixa Z do grid
			if (std::abs(0.0f - centerZ) <= gridRadius)
			{
				float xA = centerX - gridRadius;
				float xB = centerX + gridRadius;
				axisVertices.push_back(xA); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
				axisVertices.push_back(xB); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
			}
			else
			{
				// placeholder para manter os offsets dos eixos (não desenha)
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
			}

			// --- Eixo Z (y=0, x=0) ---
			// Aparece apenas se x=0 estiver dentro da faixa X do grid
			if (std::abs(0.0f - centerX) <= gridRadius)
			{
				float zA = centerZ - gridRadius;
				float zB = centerZ + gridRadius;
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(zA);
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(zB);
			}
			else
			{
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
			}

			// --- Eixo Y (x=0, z=0) ---
			// Aparece apenas se x=0 e z=0 estiverem dentro da área do grid
			if (std::abs(0.0f - centerX) <= gridRadius && std::abs(0.0f - centerZ) <= gridRadius)
			{
				axisVertices.push_back(0.0f); axisVertices.push_back(-gridRadius); axisVertices.push_back(0.0f);
				axisVertices.push_back(0.0f); axisVertices.push_back( gridRadius); axisVertices.push_back(0.0f);
			}
			else
			{
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
				axisVertices.push_back(0.0f); axisVertices.push_back(0.0f); axisVertices.push_back(0.0f);
			}

			glBindBuffer(GL_ARRAY_BUFFER, m_AxisVBO);
			glBufferData(GL_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(axisVertices.size() * sizeof(float)),
				axisVertices.data(), GL_DYNAMIC_DRAW);
			m_AxisCacheValid = true;
		}

		const glm::vec3 axisColors[] = {
			glm::vec3(0.92f, 0.16f, 0.16f),
			glm::vec3(0.18f, 0.86f, 0.35f),
			glm::vec3(0.18f, 0.40f, 1.00f)
		};
		glBindBuffer(GL_ARRAY_BUFFER, m_AxisVBO);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
		if (m_locNdcBias >= 0) glUniform1f(m_locNdcBias, -0.0008f);
		glLineWidth(3.0f);
		for (int axis = 0; axis < 3; ++axis)
		{
			if (m_locColor >= 0)
				glUniform3fv(m_locColor, 1, glm::value_ptr(axisColors[axis]));
			glDrawArrays(GL_LINES, axis * 2, 2);
		}
		glLineWidth(1.0f);
		if (m_locNdcBias >= 0) glUniform1f(m_locNdcBias, 0.0f);
	}

	// -------------------------------------------------------------------
	// Restaura estado GL
	// -------------------------------------------------------------------
	glDepthMask(prevDepthMask);
	glDepthFunc(prevDepthFunc);
	if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (prevBlend)     glEnable(GL_BLEND);      else glDisable(GL_BLEND);
	glBlendFunc(prevBlendSrc, prevBlendDst);
	glPolygonMode(GL_FRONT, prevPolygonMode[0]);
	glPolygonMode(GL_BACK, prevPolygonMode[1]);
	glLineWidth(prevLineWidth);

	glActiveTexture(prevActiveTexture);
	glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
	glBindVertexArray(static_cast<GLuint>(prevVAO));
	glUseProgram(static_cast<GLuint>(prevProgram));
}

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class GridRenderer
{
public:
	GridRenderer();
	~GridRenderer();

	bool Create();
	void Destroy();

	void Render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camPos,
		bool showAxis = true, bool showGrid = true, float gridRenderDistance = 500.0f);

private:
	GLuint m_Program = 0;
	GLuint m_VAO = 0;
	GLuint m_MinorVBO = 0;
	GLuint m_MajorVBO = 0;
	GLuint m_AxisVBO = 0;

	std::vector<float> m_MinorVertices;
	std::vector<float> m_MajorVertices;
	int m_LastCenterXi = 0;
	int m_LastCenterZi = 0;
	GLsizei m_MinorVertexCount = 0;
	GLsizei m_MajorVertexCount = 0;
	bool m_GridCacheValid = false;
	float m_LastGridRadius = 0.0f;
	float m_AxisSpan = 0.0f;
	int m_LastAxisCenterXi = 0;
	int m_LastAxisCenterZi = 0;
	bool m_AxisCacheValid = false;
	float m_LastAxisGridRadius = 0.0f;

	// uniform locations (line shader)
	GLint m_locView = -1;
	GLint m_locProjection = -1;
	GLint m_locColor = -1;
	GLint m_locNdcBias = -1;
	GLint m_locCameraPos = -1;
	GLint m_locFadeStart = -1;
	GLint m_locFadeEnd = -1;
	GLint m_locGridSpacing = -1;
	GLint m_locLineWidth = -1;
	GLint m_locMajorStep = -1;
	GLint m_locGridColorMinor = -1;
	GLint m_locGridColorMajor = -1;
	GLint m_locAxisXColor = -1;
	GLint m_locAxisYColor = -1;
	GLint m_locAxisZColor = -1;
};

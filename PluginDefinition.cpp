#include <FFGL.h>
#include <FFGLLib.h>

#include "PluginDefinition.h"

#include <fstream>
#include <sstream>
#include <cmath>

#include <sys/time.h>

#include "shader_vert.glsl"


Parameter::Parameter(string name, float min, float max, float value, int type)
: Name(name)
, Type(type)
, RangeMin(min)
, RangeMax(max)
, UniformLocation(-1)
{
    Value = (value - min) / (max - min);
}

float Parameter::GetScaledValue() const {
    return RangeMin + Value * (RangeMax - RangeMin);
}

ShaderPlugin::ShaderPlugin(int nInputs)
: CFreeFrameGLPlugin()
, m_initResources(1)
, m_inputTextureLocationArray(nullptr)
, m_nInputs(nInputs)
{
    SetMinInputs(m_nInputs);
    SetMaxInputs(m_nInputs);
    
    if (m_nInputs > 0) {
        m_inputTextureLocationArray = (GLint*)malloc(sizeof(GLint) * m_nInputs);
        for (int ii = 0; ii < m_nInputs; ii++) {
            m_inputTextureLocationArray[ii] = -1;
        }
    }
        
    SetTimeSupported(true);
    m_HostSupportsSetTime = false;
    
    m_time = 0;
    m_timeLocation = -1;
    
    m_resolution[0] = 0;
    m_resolution[1] = 0;
    m_resolution[2] = 0;
    m_resolutionLocation = -1;

    InitParameters();

    for (int ii = 0; ii < m_parameters.size(); ii++) {
        auto p = m_parameters[ii];
        SetParamInfo(ii, p.Name.c_str(), p.Type, p.Value);
    }
}

SourcePlugin::SourcePlugin()
: ShaderPlugin(0)
{
}
int SourcePlugin::Type = FF_SOURCE;


EffectPlugin::EffectPlugin()
: ShaderPlugin(1)
{
}
int EffectPlugin::Type = FF_EFFECT;

void ShaderPlugin::InitParameters()
{
    m_parameters.assign(shaderParameters.begin(), shaderParameters.end());    
}

DWORD ShaderPlugin::InitGL(const FFGLViewportStruct *vp)
{
    m_extensions.Initialize();
    if (m_extensions.multitexture==0 || m_extensions.ARB_shader_objects==0)
        return FF_FAIL;
    
    m_shader.SetExtensions(&m_extensions);
    m_shader.Compile(vertexShaderCode, fragmentShaderCode);
 
    m_shader.BindShader();
    
    for (auto& p : m_parameters) {
        p.UniformLocation = m_shader.FindUniform(p.Name.c_str());
        if (p.UniformLocation < 0) {
            fprintf(stderr, "Could not locate uniform %s in shader!", p.Name.c_str());
        }
    }
    
    for (int ii = 0; ii < m_nInputs; ii++) {
        stringstream uniformName;
        uniformName << "inputTexture" << ii;
        m_inputTextureLocationArray[ii] = m_shader.FindUniform(uniformName.str().c_str());
        m_extensions.glUniform1iARB(m_inputTextureLocationArray[ii], 0);
    }
        
    m_timeLocation = m_shader.FindUniform("iGlobalTime");
    m_resolutionLocation = m_shader.FindUniform("iResolution");
    m_resolution[0] = vp->width;
    m_resolution[1] = vp->height;
    
    m_shader.UnbindShader();
       
    return FF_SUCCESS;
}

DWORD ShaderPlugin::DeInitGL()
{
  m_shader.FreeGLResources();

  return FF_SUCCESS;
}

DWORD ShaderPlugin::ProcessOpenGL(ProcessOpenGLStruct *pGL) {
    
    if (pGL->numInputTextures < m_nInputs)
        return FF_FAIL;
    for (int ii = 0; ii < m_nInputs; ii++) {
        if (pGL->inputTextures[ii] == nullptr)
            return FF_FAIL;
    }
    
    m_shader.BindShader();
    
    for (int ii = 0; ii < m_nInputs; ii++) {
        FFGLTextureStruct &Texture = *(pGL->inputTextures[ii]);
        m_extensions.glActiveTexture(GL_TEXTURE0 + ii);
        glBindTexture(GL_TEXTURE_2D, Texture.Handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    
    for (auto& p : m_parameters) {
        m_extensions.glUniform1fARB(p.UniformLocation, p.GetScaledValue());
    }
    
    if (!m_HostSupportsSetTime)
    {
        update_time(&m_time, m_startTime);
    }
    
    m_extensions.glUniform1fARB(m_timeLocation, m_time);
    m_extensions.glUniform3fvARB(m_resolutionLocation, 3, m_resolution);
        
    EmitGeometry();
  
    for (int ii = 0; ii < m_nInputs; ii++) {
        m_extensions.glActiveTexture(GL_TEXTURE0 + ii);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    m_shader.UnbindShader();
    
    return FF_SUCCESS;
}

void ShaderPlugin::EmitGeometry()
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
	glVertex2f(-1, -1);
    glTexCoord2f(0, 1);
	glVertex2f(-1, 1);
    glTexCoord2f(1, 1);
	glVertex2f(1, 1);
    glTexCoord2f(1, 0);
	glVertex2f(1, -1);
	glEnd();
}

DWORD ShaderPlugin::GetParameter(DWORD dwIndex)
{
	DWORD dwRet;

    if (dwIndex < m_parameters.size()) {
        auto p = m_parameters[dwIndex];
        *((float *)(unsigned)&dwRet) = p.Value;
        return dwRet;
    } else {
        return FF_FAIL;
	}
}

DWORD ShaderPlugin::SetParameter(const SetParameterStruct* pParam)
{
    if (pParam != NULL && pParam->ParameterNumber < m_parameters.size()) {
        auto& p = m_parameters[pParam->ParameterNumber];
        p.Value = *((float *)(unsigned)&(pParam->NewParameterValue));
        return FF_SUCCESS;
    } else {
        return FF_FAIL;
    }
}

DWORD ShaderPlugin::SetTime(double time)
{
    m_HostSupportsSetTime = true;
    m_time = time;
    return FF_SUCCESS;
}

void update_time(double *t, const double t0)
{
#ifdef _WIN32
    //amount of time since plugin init, in seconds (same as SetTime):
    *t = double(GetTickCount())/1000.0 - t0;
#else
    timeval time;
    gettimeofday(&time, NULL);
    long millis = (time.tv_sec * 1000) + (time.tv_usec / 1000);
    *t = double(millis)/1000.0f - t0;
#endif
    return;
}


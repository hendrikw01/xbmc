/*
 *      Copyright (C) 2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "system.h"
#include "Texture.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/GLUtils.h"
#include "guilib/TextureManager.h"
#include "utils/URIUtils.h"
#include "filesystem/SpecialProtocol.h"
#include "filesystem/File.h"
#include "guilib/GraphicContext.h"
#include "settings/AdvancedSettings.h"
#include "settings/DisplaySettings.h"
#include "Application.h"
#include "threads/SingleLock.h"
#include "utils/URIUtils.h"

#include "cedarJpegLib.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>

using namespace std;

/************************************************************************/
/*    CCedarTexture                                                       */
/************************************************************************/
CCriticalSection CCedarTexture::m_critSection;

CCedarTexture::CCedarTexture(unsigned int width, unsigned int height, unsigned int format)
   : CGLTexture(width, height, format), m_egl_image(NULL), m_fallback_gl(true)
{
  memset(&m_jpeg, 0, sizeof(m_jpeg));
  EGLContext eglC = EGL_NO_CONTEXT;
      //if not created on render thread than a different OpenGL context has to be created
#if 0
  if(g_application.IsCurrentThread() == false)
     eglC = g_Windowing.GetEGLContext();
#endif

  EGLDisplay eglD = g_Windowing.GetEGLDisplay();
  cedarInitJpeg(&m_jpeg, eglD, eglC);
}

CCedarTexture::~CCedarTexture()
{
  cedarDestroyJpeg(&m_jpeg);
  m_egl_image = NULL;
}

void CCedarTexture::Allocate(unsigned int width, unsigned int height, unsigned int format)
{
  if (m_fallback_gl == false)
  {
    m_imageWidth = m_originalWidth = width;
    m_imageHeight = m_originalHeight = height;
    m_format = format;
    m_orientation = 0;

    m_textureWidth = m_imageWidth;
    m_textureHeight = m_imageHeight;
  }
  else
    return CGLTexture::Allocate(width, height, format);
}

void CCedarTexture::LoadToGPU()
{
  if ( m_fallback_gl == false)
  {
    if (m_loadedToGPU)
    {
      // nothing to load - probably same image (no change)
      return;
    }
    if (m_texture == 0)
    {
      // Have OpenGL generate a texture object handle for us
      // this happens only one time - the first time the texture is loaded
      CreateTextureObject();
    }

    if( m_texture )
    {
       // Bind the texture object
       glBindTexture(GL_TEXTURE_2D, m_texture);
      
       m_loadedToGPU = true;
    }
  }
  else 
    CGLTexture::LoadToGPU();
}
void CCedarTexture::CreateTextureObject()
{
   if (m_fallback_gl == false)
   {
      if(!m_texture)
      {
         cedarGetTexture(&m_jpeg, &m_texture);
      }
   }
   else
      CGLTexture::CreateTextureObject();
}

void CCedarTexture::Update(unsigned int width, unsigned int height, unsigned int pitch, unsigned int format, const unsigned char *pixels, bool loadToGPU)
{
  if (m_fallback_gl == false)
  {
    if (loadToGPU)
      LoadToGPU();
  }
  else
    CGLTexture::Update(width, height, pitch, format, pixels, loadToGPU);
}

bool CCedarTexture::LoadFromFileInternal(const CStdString& texturePath, unsigned int maxWidth, unsigned int maxHeight, bool autoRotate, bool requirePixels, const std::string& strMimeType)
{
#if 0
   string file = CSpecialProtocol::TranslatePath(texturePath);
   if (URIUtils::HasExtension(texturePath, ".jpg|.tbn") && ! URIUtils::IsURL(file))
   {
      CSingleLock lock(m_critSection);
    m_fallback_gl = false;
    cedarLoadJpeg(CSpecialProtocol::TranslatePath(texturePath).c_str(), &m_jpeg);
    bool okay = false;
    int orientation = 0;


#if 0  
      orientation = file->GetOrientation();
      // limit the sizes of jpegs (even if we fail to decode)
      g_OMXImage.ClampLimits(maxWidth, maxHeight, file->GetWidth(), file->GetHeight(), orientation & 4);
#endif

    ClampLimits(maxWidth, maxHeight, m_jpeg.width, m_jpeg.height, orientation & 4);
    // to be improved, cedarDecodeJpeg must be checked first
    if (requirePixels)
    {
      Allocate(maxWidth, maxHeight, XB_FMT_A8R8G8B8);
      if (m_pixels && cedarDecodeJpegToMem(&m_jpeg, maxWidth, maxHeight, (char *)m_pixels))
         okay = true;
    }
    else
    {
      if (cedarDecodeJpegToTexture(&m_jpeg, maxWidth, maxHeight, &m_egl_image) && m_egl_image)
      {
         Allocate(maxWidth, maxHeight, XB_FMT_A8R8G8B8);
         okay = true;
      }
    }
    cedarCloseJpeg(&m_jpeg);
    if (okay)
    {
      m_hasAlpha = false;
      if (autoRotate)
         m_orientation = orientation;
      return true;
    }
    else
       m_fallback_gl = true;
  }
#else
   if (URIUtils::HasExtension(texturePath, ".jpg|.tbn"))
   {
      XFILE::CFile file;
      XFILE::auto_buffer buf;

      if (file.LoadFile(texturePath, buf) <= 0)
         return false;
      
      CSingleLock lock(m_critSection);
      m_fallback_gl = false;
      
      cedarLoadMem(&m_jpeg, buf.get(), buf.size());
      
      bool okay = false;
      int orientation = 0;


#if 0  
      orientation = file->GetOrientation();
      // limit the sizes of jpegs (even if we fail to decode)
      g_OMXImage.ClampLimits(maxWidth, maxHeight, file->GetWidth(), file->GetHeight(), orientation & 4);
#endif

      ClampLimits(maxWidth, maxHeight, m_jpeg.width, m_jpeg.height, orientation & 4);
    // to be improved, cedarDecodeJpeg must be checked first
      if (requirePixels)
      {
         Allocate(maxWidth, maxHeight, XB_FMT_A8R8G8B8);
         if (m_pixels && cedarDecodeJpegToMem(&m_jpeg, maxWidth, maxHeight, (char *)m_pixels))
            okay = true;
      }
      else
      {
         if (cedarDecodeJpegToTexture(&m_jpeg, maxWidth, maxHeight, &m_egl_image) && m_egl_image)
         {
            Allocate(maxWidth, maxHeight, XB_FMT_A8R8G8B8);
            okay = true;
         }
      }
      cedarCloseJpeg(&m_jpeg);
      if (okay)
      {
         m_hasAlpha = false;
         if (autoRotate)
            m_orientation = orientation;
         return true;
      }
      else
         m_fallback_gl = true;
   }

#endif
  return CGLTexture::LoadFromFileInternal(texturePath, maxWidth, maxHeight, autoRotate, requirePixels);
}

bool CCedarTexture::ClampLimits(unsigned int &width, unsigned int &height, unsigned int m_width, unsigned int m_height, bool transposed)
{ 
  RESOLUTION_INFO& res_info = CDisplaySettings::Get().GetResolutionInfo(g_graphicsContext.GetVideoResolution());
  unsigned int max_width = width;
  unsigned int max_height = height;
  const unsigned int gui_width = transposed ? res_info.iHeight:res_info.iWidth;
  const unsigned int gui_height = transposed ? res_info.iWidth:res_info.iHeight;
  const float aspect = (float)m_width / m_height;
  bool clamped = false;

  if (max_width == 0 || max_height == 0)
  { 
    max_height = g_advancedSettings.m_imageRes;

    if (g_advancedSettings.m_fanartRes > g_advancedSettings.m_imageRes)
    { // 16x9 images larger than the fanart res use that rather than the image res
      if (fabsf(aspect / (16.0f/9.0f) - 1.0f) <= 0.01f && m_height >= g_advancedSettings.m_fanartRes)
      { 
        max_height = g_advancedSettings.m_fanartRes;
      }
    }
    max_width = max_height * 16/9;
  }

  if (gui_width)
    max_width = min(max_width, gui_width);
  if (gui_height)
    max_height = min(max_height, gui_height);

  max_width  = min(max_width, 2048U);
  max_height = min(max_height, 2048U);

  width = m_width;
  height = m_height;
  if (width > max_width || height > max_height)
  {
    if ((unsigned int)(max_width / aspect + 0.5f) > max_height)
      max_width = (unsigned int)(max_height * aspect + 0.5f);
    else
      max_height = (unsigned int)(max_width / aspect + 0.5f);
    width = max_width;
    height = max_height;
    clamped = true;
  }

  return clamped;
}


/*---------------------------------------------------------------------
*
* Copyright © 2015  Minsi Chen
* E-mail: m.chen@derby.ac.uk
*
* The source is written for the Graphics I and II modules. You are free
* to use and extend the functionality. The code provided here is functional
* however the author does not guarantee its performance.
---------------------------------------------------------------------*/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>


#if defined(WIN32) || defined(_WINDOWS)
#include <Windows.h>
#include <gl/GL.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "RayTracer.h"
#include "Ray.h"
#include "Scene.h"
#include "Camera.h"
#include "perlin.h"

RayTracer::RayTracer()
{
	m_buffHeight = m_buffWidth = 0.0;
	m_renderCount = 0;
	SetTraceLevel(5);
	m_traceflag = (TraceFlags)(TRACE_AMBIENT | TRACE_DIFFUSE_AND_SPEC |
		TRACE_SHADOW | TRACE_REFLECTION | TRACE_REFRACTION);
}

RayTracer::RayTracer(int Width, int Height)
{
	m_buffWidth = Width;
	m_buffHeight = Height;
	m_renderCount = 0;
	SetTraceLevel(5);

	m_framebuffer = new Framebuffer(Width, Height);

	//default set default trace flag, i.e. no lighting, non-recursive
	m_traceflag = (TraceFlags)(TRACE_AMBIENT);
}

RayTracer::~RayTracer()
{
	delete m_framebuffer;
}

void RayTracer::DoRayTrace(Scene* pScene)
{
	Camera* cam = pScene->GetSceneCamera();

	Vector3 camRightVector = cam->GetRightVector();
	Vector3 camUpVector = cam->GetUpVector();
	Vector3 camViewVector = cam->GetViewVector();
	Vector3 centre = cam->GetViewCentre();
	Vector3 camPosition = cam->GetPosition();

	double sceneWidth = pScene->GetSceneWidth();
	double sceneHeight = pScene->GetSceneHeight();

	double pixelDX = sceneWidth / m_buffWidth;
	double pixelDY = sceneHeight / m_buffHeight;

	int total = m_buffHeight*m_buffWidth;
	int done_count = 0;

	Vector3 start;

	start[0] = centre[0] - ((sceneWidth * camRightVector[0])
		+ (sceneHeight * camUpVector[0])) / 2.0;
	start[1] = centre[1] - ((sceneWidth * camRightVector[1])
		+ (sceneHeight * camUpVector[1])) / 2.0;
	start[2] = centre[2] - ((sceneWidth * camRightVector[2])
		+ (sceneHeight * camUpVector[2])) / 2.0;

	Colour scenebg = pScene->GetBackgroundColour();

	if (m_renderCount == 0)
	{
		fprintf(stdout, "Trace start.\n");

		Colour colour;
		//TinyRay on multiprocessors using OpenMP!!!
#pragma omp parallel for schedule (dynamic, 1) private(colour)
		for (int i = 0; i < m_buffHeight; i += 1) {
			for (int j = 0; j < m_buffWidth; j += 1) {

				//calculate the metric size of a pixel in the view plane (e.g. framebuffer)
				Vector3 pixel;

				// Anti-Aliasing
				for (float x = 0.25f; x <= 1.f; x += 0.25f)
				{
					for (float y = 0.25f; y <= 1.f; y += 0.25f)
					{

						pixel[0] = start[0] + (i + x) * camUpVector[0] * pixelDY
							+ (j + y) * camRightVector[0] * pixelDX;
						pixel[1] = start[1] + (i + x) * camUpVector[1] * pixelDY
							+ (j + y) * camRightVector[1] * pixelDX;
						pixel[2] = start[2] + (i + x) * camUpVector[2] * pixelDY
							+ (j + y) * camRightVector[2] * pixelDX;

						/*
						* setup first generation view ray
						* In perspective projection, each view ray originates from the eye (camera) position
						* and pierces through a pixel in the view plane
						*/
						Ray viewray;
						viewray.SetRay(camPosition, (pixel - camPosition).Normalise());

						double u = (double)j / (double)m_buffWidth;
						double v = (double)i / (double)m_buffHeight;

						scenebg = pScene->GetBackgroundColour();

						//trace the scene using the view ray
						//default colour is the background colour, unless something is hit along the way
						colour = this->TraceScene(pScene, viewray, scenebg, m_traceLevel);

					}
				}
				/*
				* Draw the pixel as a coloured rectangle
				*/
				m_framebuffer->WriteRGBToFramebuffer(colour, j, i);
			}
		}

		fprintf(stdout, "Done!!!\n");
		m_renderCount++;
	}
}

Colour RayTracer::TraceScene(Scene* pScene, Ray& ray, Colour incolour, int tracelevel, bool shadowray)
{
	RayHitResult result;

	Colour outcolour = incolour; //the output colour based on the ray-primitive intersection

	std::vector<Light*> *light_list = pScene->GetLightList();
	Vector3 cameraPosition = pScene->GetSceneCamera()->GetPosition();

	if (tracelevel <= 0)
	{
		return outcolour;
	}

	//Intersect the ray with the scene
	result = pScene->IntersectByRay(ray);

	if (result.data) //the ray has hit something
	{
		outcolour = CalculateLighting(light_list, &cameraPosition, &result); 
		
		if (m_traceflag & TRACE_REFLECTION)
		{
			//If the m_primtype is PRIMTYPE_Sphere or PRIMTYPE_Box enter into the statement
			if (((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Sphere 
				|| ((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Box)
			{
				//Set the direction and the origin of the ray
				Vector3 rayDirection = ray.GetRay().Reflect(result.normal);
				Vector3 rayOrigin = result.point;
				Ray reflectiveRay;
				reflectiveRay.SetRay(rayOrigin + rayDirection, rayDirection);

				//Set the new outcolour
				outcolour = TraceScene(pScene, reflectiveRay, incolour, --tracelevel, shadowray) * outcolour;
			}
		}

		if (m_traceflag & TRACE_REFRACTION)
		{
			//If the m_primtype is PRIMTYPE_Sphere or PRIMTYPE_Box enter into the statement
			if (((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Sphere 
				|| ((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Box)
			{
				//Set the direction and the origin of the ray
				Vector3 rayOrigin = result.point;
				Vector3 rayDirection = ray.GetRay().Refract(result.normal, 0.9);
				Ray refractionRay;
				refractionRay.SetRay(result.point + rayDirection * 0.01, rayDirection);

				//Set the new outcolour
				outcolour = (outcolour * 0.2) + (TraceScene(pScene, refractionRay, incolour, --tracelevel, shadowray) * 0.8);
			}
		}

		if (m_traceflag & TRACE_SHADOW)
		{
			std::vector<Light*>::iterator lit_iter = light_list->begin();

			//Iterate through the light list to get all the light positions
			while (lit_iter != light_list->end())
			{
				//Set the direction and the origin of the ray
				Vector3 rayDirection = (*lit_iter)->GetLightPosition() - result.point;
				Vector3 rayOrigin = result.point + (rayDirection * 0.0001);
				Ray shadowTest;
				shadowTest.SetRay(rayOrigin, rayDirection);
				RayHitResult shadow = pScene->IntersectByRay(shadowTest);

				//If the Materials CastShadow is true enter the statement
				outcolour = (((Primitive*)shadow.data)->GetMaterial()->CastShadow() == true) 
					? outcolour * 0.5f 
					: outcolour;

				lit_iter++;
			}
		}
		return outcolour;
	}
}

Colour RayTracer::CalculateLighting(std::vector<Light*>* lights, Vector3* campos, RayHitResult* hitresult)
{
	Colour outcolour;
	std::vector<Light*>::iterator lit_iter = lights->begin();

	Primitive* prim = (Primitive*)hitresult->data;
	Material* mat = prim->GetMaterial();

	outcolour = mat->GetAmbientColour();

	//Generate the grid pattern on the plane
	if (((Primitive*)hitresult->data)->m_primtype == Primitive::PRIMTYPE_Plane)
	{
		int dx = hitresult->point[0] / 2.0;
		int dy = hitresult->point[1] / 2.0;
		int dz = hitresult->point[2] / 2.0;

		outcolour = (dx % 2 || dy % 2 || dz % 2) ? Vector3(0.1, 0.1, 0.1) : mat->GetDiffuseColour();
	}

	if (m_traceflag & TRACE_DIFFUSE_AND_SPEC)
	{
		//If the primitive type is PRIMTYPE_Plane then return outColour as it is.
		if (((Primitive*)hitresult->data)->m_primtype != Primitive::PRIMTYPE_Plane)
		{
			//Iterate through the light list until the end has been reached
			for (int i = 0; i < 1; i++)
			{
				//Calculate the light vector, view direction vector and reflection vector
				Vector3 lightVector = (lit_iter[i]->GetLightPosition() - hitresult->point).Normalise();

				//Calculates the Diffuse reflection
				float cosAngle = (lightVector.DotProduct(hitresult->normal));
				Colour diffReflection = mat->GetDiffuseColour() * lit_iter[i]->GetLightColour() * cosAngle;

				//If the m_primtype is not PRIMTYPE_Box enter into the statement
				//if (((Primitive*)hitresult->data)->m_primtype != Primitive::PRIMTYPE_Box)
				//{
					//Specular Reflectance (Phong Model)
					//Vector3 viewDirectionVector = (hitresult->point - *campos).Normalise();
					//Vector3 reflection = lightVector.Reflect(hitresult->normal);
					//float angle = (viewDirectionVector).DotProduct(reflection);
					//angle = (angle < 0) ? 0 : (angle > 1) ? 1 : angle;
					//Colour specular = mat->GetSpecularColour() * lit_iter[i]->GetLightColour() * pow(angle, mat->GetSpecPower());

					//Specular Reflectance (Blinn-Phong Model)
					Vector3 viewDirectionVector = (*campos - hitresult->point).Normalise();
					Vector3 lightPlusViewVector = (lightVector + viewDirectionVector);
					Vector3 halfVector = lightPlusViewVector / lightPlusViewVector.Norm();
					float halfAngle = (max(min(halfVector.DotProduct(hitresult->normal), 1), 0));
					Colour specular = mat->GetSpecularColour() * lit_iter[i]->GetLightColour() * pow(halfAngle, mat->GetSpecPower());

					//Return the current outcolour plus the specular reflectance and diffuse reflection
					outcolour = outcolour + specular + diffReflection;
				//}
				//else
				//{
				//	outcolour = outcolour + diffReflection;
				//}
			}
		}
	}

	return outcolour;
}


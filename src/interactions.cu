#include "interactions.h"

#include "utilities.h"

#include <thrust/random.h>

__host__ __device__ glm::vec3 calculateRandomDirectionInHemisphere(
    glm::vec3 normal,
    thrust::default_random_engine &rng)
{
    thrust::uniform_real_distribution<float> u01(0, 1);

    float up = sqrt(u01(rng)); // cos(theta)
    float over = sqrt(1 - up * up); // sin(theta)
    float around = u01(rng) * TWO_PI;

    // Find a direction that is not the normal based off of whether or not the
    // normal's components are all equal to sqrt(1/3) or whether or not at
    // least one component is less than sqrt(1/3). Learned this trick from
    // Peter Kutz.

    glm::vec3 directionNotNormal;
    if (abs(normal.x) < SQRT_OF_ONE_THIRD)
    {
        directionNotNormal = glm::vec3(1, 0, 0);
    }
    else if (abs(normal.y) < SQRT_OF_ONE_THIRD)
    {
        directionNotNormal = glm::vec3(0, 1, 0);
    }
    else
    {
        directionNotNormal = glm::vec3(0, 0, 1);
    }

    // Use not-normal direction to generate two perpendicular directions
    glm::vec3 perpendicularDirection1 =
        glm::normalize(glm::cross(normal, directionNotNormal));
    glm::vec3 perpendicularDirection2 =
        glm::normalize(glm::cross(normal, perpendicularDirection1));

    return up * normal
        + cos(around) * over * perpendicularDirection1
        + sin(around) * over * perpendicularDirection2;
}

__host__ __device__ void scatterRay(
    PathSegment & pathSegment,
    glm::vec3 intersect,
    glm::vec3 normal,
    const Material &m,
    thrust::default_random_engine &rng)
{
    // A basic implementation of pure-diffuse shading will just call the
    // calculateRandomDirectionInHemisphere defined above.

    if (m.emittance > 0.0f) {
      // if light, end
      pathSegment.remainingBounces = 0;
      pathSegment.color *= m.emittance * m.color;
    }
    else if (m.hasReflective > 0.0f) {
      pathSegment.remainingBounces -= 1;
      pathSegment.ray.origin = intersect + (normal * 0.0001f);

      thrust::uniform_real_distribution<float> u01(0, 1);
      float randVal = u01(rng);

      if (randVal < m.hasReflective) {
        pathSegment.color *= (m.specular.color / m.hasReflective);

        // perfectly specular
        if (m.specular.exponent == -1.0f) {
          pathSegment.ray.direction = glm::normalize(glm::reflect(pathSegment.ray.direction, normal));
        }
        else {
          float xi1 = u01(rng);
          float xi2 = u01(rng);

          float cosTheta = pow(xi1, 1 / (m.specular.exponent + 1));
          float sinTheta = sqrt(max(0.0f, 1.0f - cosTheta * cosTheta));
          float phi = 2 * PI * xi2;

          float xs = cos(phi) * sinTheta;
          float ys = sin(phi) * sinTheta;
          float zs = cosTheta;

          glm::vec3 perfectReflection = glm::normalize(glm::reflect(pathSegment.ray.direction, normal));

          glm::vec3 up = glm::vec3(1.0f, 0.0f, 0.0f);
          if (abs(perfectReflection.z) < 0.999f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
          }

          glm::vec3 tangent = glm::normalize(glm::cross(up, perfectReflection));
          glm::vec3 bitangent = glm::cross(perfectReflection, tangent);

          pathSegment.ray.direction = glm::normalize(tangent * xs + bitangent * ys + perfectReflection * zs);
        }
      }
      else {
        pathSegment.color *= (m.color / 1.0f - m.hasReflective);
        pathSegment.ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
      }
    } else if (m.hasRefractive > 0.0f) {
      // TODO: add refrative, rn default to pure diffuse material
      pathSegment.remainingBounces -= 1;
      pathSegment.color *= m.color;
      pathSegment.ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
      pathSegment.ray.origin = intersect + (normal * 0.0001f);

    }
    else {
      // pure diffuse material
      pathSegment.remainingBounces -= 1;
      pathSegment.color *= m.color;
      pathSegment.ray.direction = calculateRandomDirectionInHemisphere(normal, rng);
      pathSegment.ray.origin = intersect + (normal * 0.0001f);
    }
}

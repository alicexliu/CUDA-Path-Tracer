#include "scene.h"
#include "utilities.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp> 
#include <glm/gtc/type_ptr.hpp>     
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/glm.hpp>

#include "json.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;
using json = nlohmann::json;

Scene::Scene(string filename)
{
    cout << "Reading scene from " << filename << " ..." << endl;
    cout << " " << endl;
    auto ext = filename.substr(filename.find_last_of('.'));
    if (ext == ".json")
    {
        loadFromJSON(filename);
        return;
    }
    else
    {
        cout << "Couldn't read from " << filename << endl;
        exit(-1);
    }
}

void Scene::loadFromJSON(const std::string& jsonName)
{
    std::ifstream f(jsonName);
    json data = json::parse(f);
    const auto& materialsData = data["Materials"];
    std::unordered_map<std::string, uint32_t> MatNameToID;
    for (const auto& item : materialsData.items())
    {
        const auto& name = item.key();
        const auto& p = item.value();
        Material newMaterial{};

        const auto& col = p["RGB"];
        newMaterial.color = glm::vec3(col[0], col[1], col[2]);
        newMaterial.indexOfRefraction = 1.5f;

        if (p.contains("IOR")) {
          newMaterial.indexOfRefraction = p["IOR"];
        }

        // handle materials loading differently
        if (p["TYPE"] == "Diffuse")
        {
          newMaterial.metallic = 0.0f;
          newMaterial.roughness = 1.0f;
        }
        else if (p["TYPE"] == "Emitting")
        {
            newMaterial.emittance = newMaterial.color * (float)p["EMITTANCE"];
        }
        else if (p["TYPE"] == "Specular")
        {
            newMaterial.metallic = 1.0f;
            newMaterial.roughness = p["ROUGHNESS"];
        }
        else if (p["TYPE"] == "Refractive") {
            newMaterial.metallic = 0.0f;
            newMaterial.roughness = 0.0f;
            newMaterial.transmission = 1.0f;
        }
        MatNameToID[name] = materials.size();
        materials.emplace_back(newMaterial);
    }
    const auto& objectsData = data["Objects"];
    for (const auto& p : objectsData)
    {
        const auto& type = p["TYPE"];
        Geom newGeom;
        newGeom.materialid = -1;
        if (p.contains("MATERIAL")) {
          newGeom.materialid = MatNameToID[p["MATERIAL"]];
        }

        const auto& trans = p["TRANS"];
        const auto& rotat = p["ROTAT"];
        const auto& scale = p["SCALE"];
        newGeom.translation = glm::vec3(trans[0], trans[1], trans[2]);
        newGeom.rotation = glm::vec3(rotat[0], rotat[1], rotat[2]);
        newGeom.scale = glm::vec3(scale[0], scale[1], scale[2]);

        newGeom.transform = utilityCore::buildTransformationMatrix(
        newGeom.translation, newGeom.rotation, newGeom.scale);
        newGeom.inverseTransform = glm::inverse(newGeom.transform);
        newGeom.invTranspose = glm::inverseTranspose(newGeom.transform);

        if (type == "cube")
        {
            newGeom.type = CUBE;
            geoms.push_back(newGeom);
        }
        else if (type == "sphere")
        {
            newGeom.type = SPHERE;
            geoms.push_back(newGeom);
        }
        else if (type == "mesh")
        {
            newGeom.type = MESH;

            std::string gltfFilePath = p["FILE"];
            loadGLTFMesh(gltfFilePath, newGeom.materialid, newGeom.transform);
        }
    }
    const auto& cameraData = data["Camera"];
    Camera& camera = state.camera;
    RenderState& state = this->state;
    camera.resolution.x = cameraData["RES"][0];
    camera.resolution.y = cameraData["RES"][1];
    float fovy = cameraData["FOVY"];
    state.iterations = cameraData["ITERATIONS"];
    state.traceDepth = cameraData["DEPTH"];
    state.imageName = cameraData["FILE"];
    const auto& pos = cameraData["EYE"];
    const auto& lookat = cameraData["LOOKAT"];
    const auto& up = cameraData["UP"];
    camera.position = glm::vec3(pos[0], pos[1], pos[2]);
    camera.lookAt = glm::vec3(lookat[0], lookat[1], lookat[2]);
    camera.up = glm::vec3(up[0], up[1], up[2]);
    camera.lensRadius = 0.0f;
    camera.focalDistance = 0.0f;

    if (cameraData.contains("LENSRADIUS")) {
      camera.lensRadius = cameraData["LENSRADIUS"];
    }
    if (cameraData.contains("FOCALDIST")) {
      camera.focalDistance = cameraData["FOCALDIST"];
    }

    //calculate fov based on resolution
    float yscaled = tan(fovy * (PI / 180));
    float xscaled = (yscaled * camera.resolution.x) / camera.resolution.y;
    float fovx = (atan(xscaled) * 180) / PI;
    camera.fov = glm::vec2(fovx, fovy);
    camera.pixelLength = glm::vec2(2 * xscaled / (float)camera.resolution.x,
        2 * yscaled / (float)camera.resolution.y);

    camera.view = glm::normalize(camera.lookAt - camera.position);
    camera.right = glm::normalize(glm::cross(camera.view, camera.up));

    //set up render camera stuff
    int arraylen = camera.resolution.x * camera.resolution.y;
    state.image.resize(arraylen);
    std::fill(state.image.begin(), state.image.end(), glm::vec3());
}

const uint8_t* resolveGLTFData(const tg3_model* model, int accessorIndex, uint32_t& outStride) {
  if (accessorIndex < 0) {
    return nullptr;
  }

  const tg3_accessor* acc = &model->accessors[accessorIndex];

  if (acc->buffer_view < 0) {
    return nullptr;
  }

  const tg3_buffer_view* view = &model->buffer_views[acc->buffer_view];
  const tg3_buffer* buf = &model->buffers[view->buffer];

  outStride = view->byte_stride;

  return buf->data.data + view->byte_offset + acc->byte_offset;
}

glm::vec3 readVec3(const uint8_t* data, uint32_t stride, uint32_t index) {
  const float* v = reinterpret_cast<const float*>(data + index * stride);
  return glm::vec3(v[0], v[1], v[2]);
}

uint32_t readIndex(const uint8_t* data, uint32_t index, int componentType){
  if (componentType == 5121) // UNSIGNED_BYTE
  {
    return reinterpret_cast<const uint8_t*>(data)[index];
  }
  else if (componentType == 5123) // UNSIGNED_SHORT
  {
    return reinterpret_cast<const uint16_t*>(data)[index];
  }
  else if (componentType == 5125) // UNSIGNED_INT
  {
    return reinterpret_cast<const uint32_t*>(data)[index];
  }

  return 0;
}

void Scene::loadMaterials(const tg3_model* model) {
  if (model->materials_count == 0) {
    Material defaultMat;
    defaultMat.color = glm::vec3(0.8f);
    defaultMat.metallic = 0.0f;
    defaultMat.roughness = 1.0f;
    defaultMat.emittance = glm::vec3(0.0f);
    defaultMat.indexOfRefraction = 1.5f;
    defaultMat.transmission = 0.0f;
    this->materials.push_back(defaultMat);
    return;
  }

  for (uint32_t i = 0; i < model->materials_count; i++) {
    const tg3_material* tg3Mat = &model->materials[i];
    const tg3_pbr_metallic_roughness* pbr = &tg3Mat->pbr_metallic_roughness;

    Material mat;

    // color
    mat.color = glm::vec3(
      static_cast<float>(pbr->base_color_factor[0]),
      static_cast<float>(pbr->base_color_factor[1]),
      static_cast<float>(pbr->base_color_factor[2])
    );

    // metallic + roughness
    mat.metallic = static_cast<float>(pbr->metallic_factor);
    mat.roughness = static_cast<float>(pbr->roughness_factor);

    // emissive
    mat.emittance = glm::vec3(
      static_cast<float>(tg3Mat->emissive_factor[0]),
      static_cast<float>(tg3Mat->emissive_factor[1]),
      static_cast<float>(tg3Mat->emissive_factor[2])
    );

    mat.indexOfRefraction = 1.5f;
    mat.transmission = 0.0f;   

    this->materials.push_back(mat);
  }
}

void Scene::traverseNode(tg3_model* model, uint32_t nodeIndex, glm::mat4 parentTransform, int jsonMaterialId, int materialOffset) {
  const tg3_node* node = &model->nodes[nodeIndex];
  glm::mat4 localTransform = glm::mat4(1.0f);

  if (node->has_matrix) {
    float matData[16];
    for (int i = 0; i < 16; i++) {
      matData[i] = static_cast<float>(node->matrix[i]);
    }
    localTransform = glm::make_mat4(matData);
  }
  else {
    // translation
    localTransform = glm::translate(localTransform, glm::vec3(
      static_cast<float>(node->translation[0]),
      static_cast<float>(node->translation[1]),
      static_cast<float>(node->translation[2])
    ));

    // rotation
    glm::quat q(
      static_cast<float>(node->rotation[3]), // W
      static_cast<float>(node->rotation[0]), // X
      static_cast<float>(node->rotation[1]), // Y
      static_cast<float>(node->rotation[2])  // Z
    );
    localTransform *= glm::toMat4(q);

    // scale
    localTransform = glm::scale(localTransform, glm::vec3(
      static_cast<float>(node->scale[0]),
      static_cast<float>(node->scale[1]),
      static_cast<float>(node->scale[2])
    ));
  }

  glm::mat4 worldTransform = parentTransform * localTransform;

  if (node->mesh > -1) {
    traverseMesh(model, (uint32_t)node->mesh, worldTransform, jsonMaterialId, materialOffset);
  }

  for (uint32_t i = 0; i < node->children_count; i++) {
    traverseNode(model, (uint32_t)node->children[i], worldTransform, jsonMaterialId, materialOffset);
  }
}

void Scene::traverseMesh(tg3_model* model, uint32_t meshIndex, glm::mat4 worldTransform, int jsonMaterialId, int materialOffset) {
  const tg3_mesh* mesh = &model->meshes[meshIndex];
  glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

  for (uint32_t p = 0; p < mesh->primitives_count; p++) {
    const tg3_primitive* primitive = &mesh->primitives[p];

    int idxAccIdx = primitive->indices;
    int posAccIdx = -1;
    int normAccIdx = -1;

    for (uint32_t a = 0; a < primitive->attributes_count; a++) {
      const tg3_str_int_pair* attr = &primitive->attributes[a];
      std::string attrName(attr->key.data, attr->key.len);

      if (attrName == "POSITION") posAccIdx = attr->value;
      if (attrName == "NORMAL") normAccIdx = attr->value;
    }

    if (idxAccIdx == -1 || posAccIdx == -1) continue;

    uint32_t posStride = 0;
    uint32_t normStride = 0;
    uint32_t ignoredIdxStride = 0;

    const uint8_t* idxData = resolveGLTFData(model, idxAccIdx, ignoredIdxStride);
    const uint8_t* posData = resolveGLTFData(model, posAccIdx, posStride);
    const uint8_t* normData =
      normAccIdx >= 0
      ? resolveGLTFData(model, normAccIdx, normStride)
      : nullptr;

    if (posStride == 0) {
      posStride = sizeof(float) * 3;
    }

    if (normData && normStride == 0) {
      normStride = sizeof(float) * 3;
    }
      
    glm::vec3 minBound(FLT_MAX);
    glm::vec3 maxBound(-FLT_MAX);
    uint32_t startingTriangleCount = this->triangles.size();

    const tg3_accessor* idxAcc = &model->accessors[idxAccIdx];
    const int indexComponentType = idxAcc->component_type;

    // glTF mode 4 = TRIANGLES
    if (primitive->mode != 4)
    {
      continue;
    }

    // extract triangles
    for (uint32_t i = 0; i + 2 < idxAcc->count; i += 3)
    {
      uint32_t i0 = readIndex(idxData, i, indexComponentType);
      uint32_t i1 = readIndex(idxData, i + 1, indexComponentType);
      uint32_t i2 = readIndex(idxData, i + 2, indexComponentType);

      glm::vec3 p0 = readVec3(posData, posStride, i0);
      glm::vec3 p1 = readVec3(posData, posStride, i1);
      glm::vec3 p2 = readVec3(posData, posStride, i2);

      glm::vec3 wp0 = glm::vec3(worldTransform * glm::vec4(p0, 1.0f));
      glm::vec3 wp1 = glm::vec3(worldTransform * glm::vec4(p1, 1.0f));
      glm::vec3 wp2 = glm::vec3(worldTransform * glm::vec4(p2, 1.0f));

      glm::vec3 n0, n1, n2;

      if (normData)
      {
        n0 = glm::normalize(normalMatrix * readVec3(normData, normStride, i0));
        n1 = glm::normalize(normalMatrix * readVec3(normData, normStride, i1));
        n2 = glm::normalize(normalMatrix * readVec3(normData, normStride, i2));
      }
      else
      {
        glm::vec3 n = glm::normalize(glm::cross(wp1 - wp0, wp2 - wp0));
        n0 = n1 = n2 = n;
      }

      Triangle tri;

      tri.vertices[0] = wp0;
      tri.vertices[1] = wp1;
      tri.vertices[2] = wp2;

      tri.normals[0] = n0;
      tri.normals[1] = n1;
      tri.normals[2] = n2;

      this->triangles.push_back(tri);

      minBound = glm::min(minBound, glm::min(wp0, glm::min(wp1, wp2)));
      maxBound = glm::max(maxBound,glm::max(wp0, glm::max(wp1, wp2)));
    }

    Geom meshGeom;
    meshGeom.type = MESH;
    meshGeom.triangleOffset = startingTriangleCount;
    meshGeom.numTriangles = (idxAcc->count / 3);
    meshGeom.minBound = minBound;
    meshGeom.maxBound = maxBound;


    // map material
    if (jsonMaterialId >= 0) {
      meshGeom.materialid = jsonMaterialId;
    }
    else if (primitive->material >= 0) {
      meshGeom.materialid = materialOffset + primitive->material;
    }
    else {
      meshGeom.materialid = 0;
    }

    this->geoms.push_back(meshGeom);
  }
}

void Scene::loadGLTFMesh(const std::string& gltfName, int jsonMaterialId, glm::mat4 baseTransform) {
  tg3_parse_options opts;
  tg3_error_stack errors;
  tg3_model model;

  tg3_parse_options_init(&opts);
  tg3_error_stack_init(&errors);

  tg3_error_code err = tg3_parse_file(&model, &errors, gltfName.c_str(), static_cast<uint32_t>(gltfName.length()), &opts);
  if (err != TG3_OK) {
    for (uint32_t i = 0; i < errors.count; i++) {
      fprintf(stderr, "[%d] %s\n", (int)errors.entries[i].severity,
        errors.entries[i].message ? errors.entries[i].message : "(null)");
    }
  }

  // load materials if needed
  int materialOffset = this->materials.size();
  if (jsonMaterialId < 0) {
    loadMaterials(&model);
  }

  // use model
  if (model.scenes_count > 0) {
    int sceneIndex = (model.default_scene > -1) ? model.default_scene : 0;

    const tg3_scene* scene = &model.scenes[sceneIndex];

    for (uint32_t i = 0; i < scene->nodes_count; i++) {
      uint32_t rootNodeIndex = scene->nodes[i];
      traverseNode(&model, rootNodeIndex, baseTransform, jsonMaterialId, materialOffset);
    }
  }

  // free
  tg3_model_free(&model);
  tg3_error_stack_free(&errors);
  return;
}
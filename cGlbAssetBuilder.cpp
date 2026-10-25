#include "cGlbAssetBuilder.h"
#include <cmath>
#include <map>
#include <stdexcept>

namespace
{
	using V3=std::array<float,3>;
	float dot(const V3& a,const V3& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
	V3 cross(const V3& a,const V3& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
	V3 unit(V3 a)
	{
		const double length=std::sqrt(double(a[0])*a[0]+double(a[1])*a[1]+double(a[2])*a[2]);
		if (length<1e-12 || !std::isfinite(length)) throw std::runtime_error("Cannot generate a stable tangent from this geometry/UV layout.");
		for (float& x:a) x=float(x/length);
		return a;
	}
	size_t tangents(YdrMesh& mesh)
	{
		size_t collapsedUv=0;
		std::vector<YdrVertex> vertices;
		vertices.reserve(mesh.indices.size());
		for (size_t i=0;i<mesh.indices.size();i+=3)
		{
			const auto& a=mesh.vertices[mesh.indices[i]];
			const auto& b=mesh.vertices[mesh.indices[i+1]];
			const auto& c=mesh.vertices[mesh.indices[i+2]];
			const float u1=b.uv[0]-a.uv[0],v1=b.uv[1]-a.uv[1],u2=c.uv[0]-a.uv[0],v2=c.uv[1]-a.uv[1];
			const double determinant=double(u1)*v2-double(v1)*u2;
			const bool collapsed=std::fabs(determinant)<1e-20;
			if (collapsed) ++collapsedUv;
			V3 tangent,bitangent;
			for (unsigned k=0;k<3;++k)
			{
				const double e1=double(b.position[k])-a.position[k],e2=double(c.position[k])-a.position[k];
				tangent[k]=collapsed ? float(e1):float((e1*v2-e2*v1)/determinant);
				bitangent[k]=collapsed ? float(e2):float((e2*u1-e1*u2)/determinant);
			}
			for (unsigned j=0;j<3;++j)
			{
				auto vertex=mesh.vertices[mesh.indices[i+j]];
				const float projection=dot(tangent,vertex.normal);
				V3 orthogonal;
				for (unsigned k=0;k<3;++k) orthogonal[k]=tangent[k]-projection*vertex.normal[k];
				if (collapsed && dot(orthogonal,orthogonal)<1e-20)
				{
					unsigned least=0;
					for (unsigned k=1;k<3;++k) if (std::fabs(vertex.normal[k])<std::fabs(vertex.normal[least])) least=k;
					V3 axis={}; axis[least]=1; orthogonal=cross(vertex.normal,axis);
				}
				orthogonal=unit(orthogonal);
				const float sign=dot(cross(vertex.normal,orthogonal),bitangent)<0 ? -1.f:1.f;
				vertex.tangent={orthogonal[0],orthogonal[1],orthogonal[2],sign};
				vertices.push_back(vertex);
			}
		}
		mesh.vertices=std::move(vertices);
		for (size_t i=0;i<mesh.indices.size();++i) mesh.indices[i]=uint32_t(i);
		return collapsedUv;
	}
}

GlbBuiltAssets buildGlbAssets(const GlbModel& model,const GlbImportProject& project,unsigned textureSize)
{
	const auto errors=glbRigErrors(model,project);
	if (!errors.empty()) throw std::runtime_error(errors.front());
	if (!project.skeleton.binaryMetadata) throw std::runtime_error("Binary reference skeleton metadata is required for game asset export.");
	auto materials=convertGlbSourceMaterials(project.sourcePath,project.sourceSha256,project.modelName,textureSize);
	GlbBuiltAssets output;
	output.drawable.name=project.modelName; output.drawable.skeleton=project.skeleton;
	output.diagnostics.push_back("Material policy: opaque weapon_normal_spec_detail_palette with neutral detail/palette. Not verified rendering equivalence.");
	output.diagnostics.push_back("glTF metallic-roughness becomes an artistic grayscale specular approximation; shared AO is baked into base color.");
	output.diagnostics.push_back("Only high LOD is generated. Game animations, component detachment/reload, normal-map decoding and tint behavior require in-game validation.");
	auto solid=[&](const std::string& name,uint8_t r,uint8_t g,uint8_t b,YtdImageSemantic semantic) {
		YtdImage image; image.name=name; image.width=4; image.height=4; image.semantic=semantic;
		for (unsigned i=0;i<16;++i) image.rgba.insert(image.rgba.end(),{r,g,b,255});
		output.images.push_back(std::move(image));
	};
	const auto detail=project.modelName+"_detail",palette=project.modelName+"_palette";
	solid(detail,128,128,255,YtdImageSemantic::TangentNormal);
	solid(palette,255,255,255,YtdImageSemantic::ColorSrgb);
	std::map<uint16_t,uint16_t> materialMap;
	const auto parts=includedGlbParts(model,project);
	const auto preview=makePreparedGlbPreview(model,project);
	for (size_t i=0;i<parts.size();++i)
	{
		const auto& source=model.parts[parts[i]];
		const auto sourceMaterial=source.geometry.material;
		if (!materialMap.count(sourceMaterial))
		{
			auto& converted=materials.at(sourceMaterial);
			YdrMaterial m;
			m.profile=YdrShaderProfile::WeaponNormalSpecDetailPalette;
			m.diffuseTexture=converted.diffuse.name; m.normalTexture=converted.normal.name; m.specularTexture=converted.specular.name;
			m.detailTexture=detail; m.paletteTexture=palette;
			m.specularIntensity=.75f; m.specularFalloff=40; m.specularFresnel=.75f;
			materialMap[sourceMaterial]=uint16_t(output.drawable.materials.size());
			output.drawable.materials.push_back(std::move(m));
			output.images.push_back(std::move(converted.diffuse));
			output.images.push_back(std::move(converted.normal));
			output.images.push_back(std::move(converted.specular));
		}
		auto mesh=source.geometry;
		mesh.sourcePart=parts[i];
		mesh.material=materialMap.at(sourceMaterial); mesh.rigidBone=project.boneBindings[parts[i]];
		for (size_t v=0;v<mesh.vertices.size();++v)
			for (unsigned k=0;k<3;++k)
			{
				mesh.vertices[v].position[k]=preview.meshes[i].pos[v*3+k];
				mesh.vertices[v].normal[k]=preview.meshes[i].nrm[v*3+k];
			}
		size_t collapsed=0;
		if (source.hasTangents)
		{
			for (auto& vertex:mesh.vertices)
			{
				const auto rotated=glbAlignedDirection(project.alignment,{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]});
				for (unsigned k=0;k<3;++k) vertex.tangent[k]=rotated[k];
			}
		}
		// Missing tangents use face-local frames, retaining mirrored UV handedness.
		else collapsed=tangents(mesh);
		if (collapsed) output.diagnostics.push_back(source.name+": "+std::to_string(collapsed)+
			" triangles have collapsed UVs. A deterministic geometric tangent basis was generated; normal-map orientation there is undefined by the source UVs and needs visual inspection.");
		if (mesh.vertices.size()<=65535) output.drawable.meshes.push_back(std::move(mesh));
		else
		{
			for (size_t at=0;at<mesh.vertices.size();at+=65535)
			{
				YdrMesh chunk; chunk.material=mesh.material; chunk.rigidBone=mesh.rigidBone; chunk.sourcePart=mesh.sourcePart;
				const size_t end=std::min(at+65535,mesh.vertices.size());
				chunk.vertices.assign(mesh.vertices.begin()+at,mesh.vertices.begin()+end);
				for (size_t v=0;v<chunk.vertices.size();++v) chunk.indices.push_back(uint32_t(v));
				output.drawable.meshes.push_back(std::move(chunk));
			}
		}
	}
	return output;
}

GlbPartitionedAssets partitionGlbComponents(const GlbBuiltAssets& assets,const GlbImportProject& project)
{
	GlbPartitionedAssets result;
	result.weapon=assets.drawable;
	result.weapon.meshes.clear();
	const auto world=skeletonWorldMatrices(project.skeleton);
	std::map<GlbPartRole,size_t> groups;
	for (const auto& source:assets.drawable.meshes)
	{
		const auto role=project.roles.at(source.sourcePart);
		if (source.rigidBone!=project.boneBindings.at(source.sourcePart))
			throw std::runtime_error("Component partitioning requires assets from the current bone mapping.");
		const char* suffix=nullptr;
		switch (role)
		{
		case GlbPartRole::Magazine:suffix="_mag1";break;
		case GlbPartRole::Scope:suffix="_scope";break;
		case GlbPartRole::Suppressor:suffix="_supp";break;
		case GlbPartRole::Flashlight:suffix="_flash";break;
		case GlbPartRole::Grip:suffix="_grip";break;
		default:break;
		}
		if (!suffix) { result.weapon.meshes.push_back(source); continue; }
		if (source.rigidBone<0 || size_t(source.rigidBone)>=world.size()) throw std::runtime_error("Component has no valid reference attachment bone.");
		const auto bone=size_t(source.rigidBone);
		if (!groups.count(role))
		{
			GlbComponentAsset component;
			component.role=role; component.attachBone=project.skeleton.bones[bone].name;
			component.localAttachBone=project.skeleton.bones[0].name;
			component.drawable.name=normalizeAssetName(project.modelName+suffix,"Component model");
			component.drawable.materials=assets.drawable.materials;
			component.drawable.skeleton=project.skeleton;
			groups[role]=result.components.size(); result.components.push_back(std::move(component));
		}
		auto& component=result.components[groups.at(role)];
		if (component.attachBone!=project.skeleton.bones[bone].name)
			throw std::runtime_error("All pieces of one component role must use the same attachment bone.");
		auto mesh=source; mesh.rigidBone=0;
		for (auto& vertex:mesh.vertices)
		{
			vertex.position=bonePoint(world[0],inverseBonePoint(world[bone],vertex.position));
			vertex.normal=boneDirection(world[0],inverseBoneDirection(world[bone],vertex.normal));
			const auto tangent=boneDirection(world[0],inverseBoneDirection(world[bone],{vertex.tangent[0],vertex.tangent[1],vertex.tangent[2]}));
			for (unsigned k=0;k<3;++k) vertex.tangent[k]=tangent[k];
		}
		component.drawable.meshes.push_back(std::move(mesh));
	}
	if (result.weapon.meshes.empty()) throw std::runtime_error("No base weapon geometry remains after component partitioning.");
	return result;
}

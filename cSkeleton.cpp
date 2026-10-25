#include "cSkeleton.h"
#include <cmath>
#include <set>
#include <stdexcept>

namespace
{
	[[noreturn]] void fail(const char* text) { throw std::invalid_argument(std::string("Reference skeleton: ") + text); }
	BoneMatrix localMatrix(const SkeletonBone& bone)
	{
		double x=bone.rotation[0], y=bone.rotation[1], z=bone.rotation[2], w=bone.rotation[3];
		const double length = std::sqrt(x*x+y*y+z*z+w*w);
		x/=length; y/=length; z/=length; w/=length;
		return {1-2*(y*y+z*z), 2*(x*y+w*z), 2*(x*z-w*y), 0,
			2*(x*y-w*z), 1-2*(x*x+z*z), 2*(y*z+w*x), 0,
			2*(x*z+w*y), 2*(y*z-w*x), 1-2*(x*x+y*y), 0,
			bone.translation[0], bone.translation[1], bone.translation[2], 1};
	}
}

void validateReferenceSkeleton(const ReferenceSkeleton& skeleton)
{
	if (skeleton.bones.empty() || skeleton.bones.size() > 256) fail("Expected 1..256 bones.");
	std::set<std::string> names;
	std::set<uint16_t> tags;
	size_t roots = 0;
	for (size_t i=0; i<skeleton.bones.size(); ++i)
	{
		const auto& bone=skeleton.bones[i];
		if (bone.name.empty() || bone.name.size()>127 || bone.name.find_first_not_of(
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos ||
			!names.insert(bone.name).second || !tags.insert(bone.tag).second)
			fail("Bone names/tags must be unique, with ASCII bone names.");
		if (bone.parent < -1 || bone.parent >= int(i)) fail("Parents must precede children; cyclic/invalid hierarchy.");
		if (bone.sibling < -1 || bone.sibling>=int(skeleton.bones.size()) || bone.sibling==int(i))
			fail("Invalid sibling index.");
		if (bone.parent == -1) ++roots;
		for (float x : bone.translation)
			if (!std::isfinite(x) || std::fabs(x)>1000) fail("Invalid bone translation.");
		double length=0;
		for (float x : bone.rotation) { if (!std::isfinite(x)) fail("Invalid quaternion."); length+=double(x)*x; }
		if (std::fabs(length-1)>0.002) fail("Expected unit bone quaternion.");
		for (float x:bone.transformExtra) if (!std::isfinite(x) || std::fabs(x)>1000000) fail("Invalid transform-extra value.");
	}
	if (roots != 1) fail("Expected exactly one skeleton root.");
	if (skeleton.binaryMetadata && (skeleton.tagBucketCount==0 || skeleton.tagBucketCount>1024 ||
		skeleton.childPairs.empty() || skeleton.childPairs.size()>4096)) fail("Invalid reference runtime tables.");
	for (const auto& pair:skeleton.childPairs)
		if (pair[0]>=skeleton.bones.size() || pair[1]>=skeleton.bones.size() ||
			skeleton.bones[pair[0]].parent!=int(pair[1])) fail("Invalid child-pair bone index/hierarchy.");
}

std::vector<BoneMatrix> skeletonWorldMatrices(const ReferenceSkeleton& skeleton)
{
	validateReferenceSkeleton(skeleton);
	std::vector<BoneMatrix> world;
	for (const auto& bone : skeleton.bones)
	{
		auto result=localMatrix(bone);
		if (bone.parent>=0)
		{
			const auto& parent=world[size_t(bone.parent)];
			BoneMatrix product={};
			for (unsigned c=0; c<4; ++c)
				for (unsigned r=0; r<4; ++r)
					for (unsigned k=0; k<4; ++k) product[c*4+r]+=parent[k*4+r]*result[c*4+k];
			result=product;
		}
		world.push_back(result);
	}
	return world;
}

std::array<float,3> bonePoint(const BoneMatrix& matrix, const std::array<float,3>& point)
{
	std::array<float,3> out;
	for (unsigned k=0; k<3; ++k)
		out[k]=float(matrix[k]*point[0]+matrix[4+k]*point[1]+matrix[8+k]*point[2]+matrix[12+k]);
	return out;
}

std::array<float,3> inverseBonePoint(const BoneMatrix& matrix, const std::array<float,3>& point)
{
	std::array<float,3> out;
	for (unsigned k=0; k<3; ++k)
	{
		double value=0;
		for (unsigned j=0; j<3; ++j) value+=matrix[k*4+j]*(double(point[j])-matrix[12+j]);
		out[k]=float(value);
	}
	return out;
}

std::array<float,3> inverseBoneDirection(const BoneMatrix& matrix,const std::array<float,3>& direction)
{
	std::array<float,3> out;
	for (unsigned k=0;k<3;++k)
		out[k]=float(matrix[k*4]*direction[0]+matrix[k*4+1]*direction[1]+matrix[k*4+2]*direction[2]);
	return out;
}

std::array<float,3> boneDirection(const BoneMatrix& matrix,const std::array<float,3>& direction)
{
	std::array<float,3> out;
	for (unsigned k=0;k<3;++k) out[k]=float(matrix[k]*direction[0]+matrix[k+4]*direction[1]+matrix[k+8]*direction[2]);
	return out;
}

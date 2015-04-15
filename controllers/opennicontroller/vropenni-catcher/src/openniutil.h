/*
 * This file is part of VRController.
 * Copyright (c) 2015 Fabien Caylus <toutjuste13@gmail.com>
 *
 * This file is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OPENNIUTILS_H
#define OPENNIUTILS_H

#include <ni/XnTypes.h>
#include <cmath>

#include <QVector>
#include <QDebug>

#define PI 3.14159265358979323846
#define RAD2DEG (180.0/PI)

#define DEPTH_MAP_LENGTH (640*480)
#define MIN_COMPUTED_WALKSPEED 40

// Contains some structures for OpenNI data
namespace OpenNIUtil
{
    struct Joint
    {
        XnSkeletonJoint type;
        // Check if the join is active
        bool isActive = false;
        XnSkeletonJointPosition info;
        XnPoint3D projectivePos;
    };

    struct BodyPart
    {
        Joint hip;
        Joint knee;
        Joint foot;

        Joint shoulder;
    };

    struct User
    {
        XnUserID id;
        bool isTracking = false;

        // The timestamp when this object was generated
        // Represent the time in milliseconds since the epoch time
        int64_t timestamp;

        Joint torsoJoint;

        // Current body informations
        BodyPart leftPart;
        BodyPart rightPart;

        // Legs informations at the previous frame
        BodyPart previousLeftPart;
        BodyPart previousRightPart;

        int rotation = -1;
        // Represent the confidence we have in the current rotation
        // Computed by multiply the confidence of the two hip joints
        // In normal conditions, should be in range 2.56 - 4
        XnConfidence rotationConfidence = -1.0f;

        int walkSpeed = -1;
        // Represent the confidence we have in the current walk speed
        // In normal conditions, should be in range 6.5536 - 16
        XnConfidence walkSpeedConfidence = -1.0f;

        // Summary the number of frames since the last move
        int numberOfFramesWithoutMove = 0;
    };

    // Only contains informations about the users
    // Depth map are stored separately
    struct CameraInformations
    {
        User user;

        // Tell if we are using two sensors
        bool hasSecondView = false;

        User secondUser;
        int secondRotationProjected = -1;
        int averageRotation = -1;
        int averageWalkSpeed = -1;

        bool invalid = false;
    };

    struct DepthMaps
    {
        // The depth map (values are in mm)
        XnDepthPixel *depthData = nullptr;
        // Only set if there is a second user
        XnDepthPixel *secondDepthData = nullptr;

        bool invalid = false;
    };

    inline CameraInformations createInvalidCamInfo()
    {
        CameraInformations camInfo;
        camInfo.invalid = true;
        return camInfo;
    }

    inline DepthMaps createInvalidDepthMaps()
    {
        DepthMaps maps;
        maps.invalid = true;
        return maps;
    }

    inline bool isJointAcceptable(const Joint joint)
    {
        return joint.isActive && joint.info.fConfidence >= 0.6;
    }

    // Return the angle in range [0;360[
    inline float reduceAngle(const float angle)
    {
        if(angle >= 360.0f)
            return angle - 360.0f;
        else if(angle < 0.0f)
            return 360.0f + angle;
        return angle;
    }

    // Indexes of the 3x3 matrix are:
    //
    // ( 0  1  2 )   ( Xx  Yx  Zx )
    // ( 3  4  5 ) = ( Xy  Yy  Zy )
    // ( 6  7  8 )   ( Xz  Yz  Zz )
    //
    inline float orientationMatrixToRotation(XnFloat orientation[9])
    {
        // Get component Xx
        float xRot = std::acos(orientation[0]) * RAD2DEG;

        // Check the Z component of the X axis
        if(orientation[6] < 0.0f)
            xRot = 360.0f - xRot;

        xRot = reduceAngle(xRot);

        // Get component Zz
        float zRot = std::asin(orientation[8] * RAD2DEG);

        if(zRot > 0.0f)
            zRot = 360.0f - zRot;
        else
            zRot = std::abs(zRot);

        zRot = reduceAngle(zRot);

        return (xRot + zRot) / 2.0f;
    }

    // The previous rotation parameter is used to avoid big differences between two rotation
    inline float rotationFrom2Joints(const int frequency, const Joint rightJoint, const Joint leftJoint, float previousRotation, XnConfidence *resultConfidence)
    {
        if(isJointAcceptable(rightJoint) && isJointAcceptable(leftJoint))
        {
            const float angle = std::atan(std::abs(rightJoint.info.position.Z - leftJoint.info.position.Z)
                                          / std::abs(rightJoint.info.position.X - leftJoint.info.position.X)) * RAD2DEG;

            float rotation = -1.0f;

            // 0
            if(rightJoint.info.position.Z == leftJoint.info.position.Z
               && rightJoint.info.position.X > leftJoint.info.position.X)
                rotation = 0.0f;
            // 90
            else if(rightJoint.info.position.X == leftJoint.info.position.X
                    && rightJoint.info.position.Z > leftJoint.info.position.Z)
                rotation = 90.0f;
            // 180
            else if(rightJoint.info.position.Z == leftJoint.info.position.Z
                    && rightJoint.info.position.X < leftJoint.info.position.X)
                rotation = 180.0f;
            // 270
            else if(rightJoint.info.position.X == leftJoint.info.position.X
                    && rightJoint.info.position.Z < leftJoint.info.position.Z)
                rotation = 90.0f;

            // 0 - 180
            else if(rightJoint.info.position.Z < leftJoint.info.position.Z)
            {
                // 0 - 90
                if(rightJoint.info.position.X > leftJoint.info.position.X)
                    rotation = angle;
                // 90 - 180
                else
                    rotation = 180.0f - angle;
            }
            // 180 - 360
            else
            {
                // 180 - 270
                if(rightJoint.info.position.X < leftJoint.info.position.X)
                    rotation = 180.0f + angle;
                // 270 - 360
                else
                    rotation =  360.0f - angle;
            }

            //
            // Smooth the rotation
            //

            if(previousRotation != -1.0f)
            {
                // If the difference of rotation is higher than 180°, we consider
                // that we are move from the 360° to the 0° side
                // In this case, add 360° to the lower value
                if(std::abs(rotation - previousRotation) > 180.0f)
                {
                    if(rotation < previousRotation)
                        rotation += 360.0f;
                    else
                        previousRotation += 360.0f;
                }

                const float margin = 55.0f / (float)frequency;

                const float diffRotation = rotation - previousRotation;
                if(std::abs(diffRotation) > margin)
                {
                    // If new rotation is higher
                    if(diffRotation > 0.0f)
                    {
                        rotation = previousRotation + margin;
                    }
                    // If new rotation is lower
                    else
                    {
                        rotation = previousRotation - margin;
                    }
                }

                if(rotation >= 360.0f)
                    rotation -= 360.0f;
            }

            if(resultConfidence != nullptr)
                *resultConfidence = (leftJoint.info.fConfidence + 1.0f) * (rightJoint.info.fConfidence + 1.0f);

            return rotation;
        }

        if(resultConfidence != nullptr)
            *resultConfidence = -1.0f;
        return -1.0f;
    }

    inline void rotationForUser(const int frequency, const int previousRotation, User* user)
    {
        int rotations[4] = {-1, -1, -1, -1};

        // right hip / left hip
        rotations[0] = static_cast<int>(rotationFrom2Joints(frequency, user->rightPart.hip, user->leftPart.hip,
                                                            previousRotation, &(user->rotationConfidence)));
        // right hip / torso
        rotations[1] = static_cast<int>(rotationFrom2Joints(frequency, user->rightPart.hip, user->torsoJoint,
                                                            previousRotation, nullptr));
        // torso / left hip
        rotations[2] = static_cast<int>(rotationFrom2Joints(frequency, user->torsoJoint, user->leftPart.hip,
                                                            previousRotation, nullptr));
        // right shoulder / left shoulder
        rotations[3] = static_cast<int>(rotationFrom2Joints(frequency, user->rightPart.shoulder, user->leftPart.shoulder,
                                                            previousRotation, nullptr));

        // Make an average
        int rotSum = 0;
        int rotCount = 0;
        for(int i=0; i < 4; ++i)
        {
            if(rotations[i] != -1)
            {
                if(rotations[i] > 180.0f)
                    rotSum += (rotations[i] - 360.0f);
                else
                    rotSum += rotations[i];

                rotCount++;
            }
        }

        if(rotCount != 0)
            user->rotation = reduceAngle(rotSum / rotCount);
        else
            user->rotation = -1;
    }

    inline int walkSpeedForUser(const int frequency, const User& user, const int64_t& previousTimestamp, const int& previousSpeed, XnConfidence *resultConfidence)
    {
        // Compute the x and z diff for the right and left foot
        float rdx = 0;
        float rdz = 0;
        float ldx = 0;
        float ldz = 0;

        // Tell if we are not able to compute the speed
        bool cantCompute = true;

        if(isJointAcceptable(user.rightPart.foot) && isJointAcceptable(user.previousRightPart.foot))
        {
            rdx = user.previousRightPart.foot.info.position.X - user.rightPart.foot.info.position.X;
            rdz = user.previousRightPart.foot.info.position.Z - user.rightPart.foot.info.position.Z;

            if(isJointAcceptable(user.leftPart.foot) && isJointAcceptable(user.previousLeftPart.foot))
            {
                ldx = user.previousLeftPart.foot.info.position.X - user.leftPart.foot.info.position.X;
                ldz = user.previousLeftPart.foot.info.position.Z - user.leftPart.foot.info.position.Z;
                cantCompute = false;
            }
        }

        if(cantCompute)
            return -1;

        *resultConfidence = (user.rightPart.foot.info.fConfidence + 1.0f)
                * (user.previousRightPart.foot.info.fConfidence + 1.0f)
                * (user.leftPart.foot.info.fConfidence + 1.0f)
                * (user.previousLeftPart.foot.info.fConfidence + 1.0f);

        const float rightDiff = std::sqrt(std::pow(rdx, 2.0) + std::pow(rdz, 2.0));
        const float leftDiff = std::sqrt(std::pow(ldx, 2.0) + std::pow(ldz, 2.0));

        // Compute the average of diff (in mm)
        const float diff = (rightDiff + leftDiff) / 2.0;

        // Compute diff of timestamp
        const int64_t diffTime = user.timestamp - previousTimestamp;

        // Now compute the speed in cm/s
        int speed = static_cast<int>((diff * 0.1) / ((double)(diffTime) * 0.001));

        // Smooth the value depending on the last one
        if(previousSpeed != -1)
        {
            const int margin = 100.0f / (float)frequency;

            if(std::abs(speed - previousSpeed) > margin)
            {
                if(speed < previousSpeed)
                    speed = previousSpeed - margin;
                else
                    speed = previousSpeed + margin;
            }
        }

        return speed;
    }
}

#endif // OPENNIUTILS_H


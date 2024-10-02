/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _BIH_WRAP
#define _BIH_WRAP

#include "BoundingIntervalHierarchy.h"
#include "G3D/Array.h"
#include "G3D/Set.h"
#include "G3D/Table.h"

template<class T, class BoundsFunc = BoundsTrait<T>>
class BIHWrap
{
    template<class RayCallback>
    struct MDLCallback
    {
        const T* const* objects;
        RayCallback& _callback;
        uint32 objects_size;

        MDLCallback(RayCallback& callback, const T* const* objects_array, uint32 objects_size ) : objects(objects_array), _callback(callback), objects_size(objects_size) { }

        /// Intersect ray
        bool operator() (const G3D::Ray& ray, uint32 idx, float& maxDist, bool stopAtFirstHit)
        {
            if (idx >= objects_size)
            {
                return false;
            }
            if (const T* obj = objects[idx])
            {
                return _callback(ray, *obj, maxDist, stopAtFirstHit);
            }
            return false;
        }

        /// Intersect point
        void operator() (const G3D::Vector3& p, uint32 idx)
        {
            if (idx >= objects_size)
            {
                return;
            }
            if (const T* obj = objects[idx])
            {
                _callback(p, *obj);
            }
        }
    };

    typedef G3D::Array<const T*> ObjArray;

    BIH m_tree;
    ObjArray m_objects;
    G3D::Table<const T*, uint32> m_obj2Idx;
    G3D::Set<const T*> m_objects_to_push;
    int unbalanced_times;

public:
    BIHWrap() : unbalanced_times(0) { }

    void insert(const T& obj)
    {
        ++unbalanced_times;
        m_objects_to_push.insert(&obj);
    }

    void remove(const T& obj)
    {
        ++unbalanced_times;
        uint32 Idx = 0;
        const T* temp;
        if (m_obj2Idx.getRemove(&obj, temp, Idx))
        {
            m_objects[Idx] = nullptr;
        }
        else
        {
            m_objects_to_push.remove(&obj);
        }
    }

    void balance()
    {
        if (unbalanced_times == 0)
        {
            return;
        }

        unbalanced_times = 0;
        m_objects.fastClear();
        //m_obj2Idx.getKeys(m_objects);
            // 重新填充 m_objects，确保 m_obj2Idx 内部是有效的,防止宕机
        if (m_obj2Idx.size() > 0)
        {
            m_obj2Idx.getKeys(m_objects);
        }
        else
        {
            // 添加断言或日志
            assert(false && "m_obj2Idx is empty!");
            return;
        }
        //m_objects_to_push.getMembers(m_objects);
            // 检查 m_objects_to_push 的有效性
        if (m_objects_to_push.size() > 0)
        {
            m_objects_to_push.getMembers(m_objects);
        }
        else
        {
            // 添加断言或日志
            assert(false && "m_objects_to_push is empty!");
            return;
        }
        //assert that m_obj2Idx has all the keys
            // 在构建 BIH 树之前确保 m_objects 是有效的
        if (m_objects.size() > 0)
        {
            m_tree.build(m_objects, BoundsFunc::GetBounds2);
        }
        else
        {
            // 添加断言或日志
            assert(false && "m_objects is empty after push!");
        }

        //m_tree.build(m_objects, BoundsFunc::GetBounds2);
    }

    template<typename RayCallback>
    void intersectRay(const G3D::Ray& ray, RayCallback& intersectCallback, float& maxDist, bool stopAtFirstHit)
    {
        balance();
        MDLCallback<RayCallback> temp_cb(intersectCallback, m_objects.getCArray(), m_objects.size());
        m_tree.intersectRay(ray, temp_cb, maxDist, stopAtFirstHit);
    }

    template<typename IsectCallback>
    void intersectPoint(const G3D::Vector3& point, IsectCallback& intersectCallback)
    {
        balance();
        MDLCallback<IsectCallback> callback(intersectCallback, m_objects.getCArray(), m_objects.size());
        m_tree.intersectPoint(point, callback);
    }
};

#endif // _BIH_WRAP

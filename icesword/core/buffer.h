/* Copyright (c) 2018 NoobsDNN, Anakin Authors, Inc. All Rights Reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef NBDNN_ICESWORD_CORE_BUFFER_H
#define NBDNN_ICESWORD_CORE_BUFFER_H

#include "icesword/core/target_wrapper.h"
#include "icesword/core/data_traits.h"

namespace noobsdnn{

namespace icesword{

template <TargetType TType>
class Buffer {
public:
    typedef typename DataTraitBase<TType>::PtrDtype TPtr;
    typedef TargetWrapper<TType> API;
    //typedef typename TargetTypeTraits<TType>::target_type target_type;

    Buffer() : _data(nullptr), _own_data(true), _count(0), _capacity(0){
        _id = API::get_device_id();
    }

    /**
     * \brief constructor with buffer size, in Dtype
     */
    explicit Buffer(size_t size)
    	: _data(nullptr), _own_data(true), _count(size), _capacity(size){
    	ICESWORD_CHECK(alloc(size));
        _id = API::get_device_id();
    }

    explicit Buffer(TPtr data, size_t size, int id)
    	: _own_data(false), _count(size), _capacity(size){
        _data = data;
        _id = API::get_device_id();
        CHECK_EQ(id, _id) << "data is not in current device";
    }

    /**
     * \brief copy constructor
     */
    Buffer(Buffer<TType>& buf){
        CHECK_GT(buf._count, 0) << "input buffer is empty";
        _count = buf._count;
        _id = API::get_device_id();
        if (buf._id == _id){
            _data = buf._data;
            _own_data = false;
            _capacity = _count;
        } else{
            _own_data = true;
            ICESWORD_CHECK(re_alloc(buf._count));
            API::sync_memcpy_p2p(_data, 0, _id, buf.get_data(), 0, buf._id, buf._count);
        }
    }

    /**
     * \brief assigned function, ptop memcpy is called if src is in different device
     */
    Buffer& operator = (Buffer<TType>& buf){
        this->_count = buf._count;
        this->_id = API::get_device_id();
        if (buf._id == this->_id){
            this->_data = buf._data;
            this->_capacity = this->_count;
            this->_own_data = false;
        } else{
            this->_own_data = true;
            ICESWORD_CHECK(this->re_alloc(buf._count));
            API::sync_memcpy_p2p(this->_data, 0, this->_id, buf.get_data(), 0, buf._id, \
                buf._count);
        }
        return *this;
    }

    int shared_from(Buffer<TType>& buf){
        _count = buf._count;
        _id = API::get_device_id();
        if (buf._id == _id){
            _data = buf._data;
            _capacity = _count;
            _own_data = false;
            return 1;
        } else{
            _own_data = true;
            ICESWORD_CHECK(re_alloc(buf._count));
            API::sync_memcpy_p2p(_data, 0, _id, buf.get_data(), 0, buf._id, buf._count);
            return 0;
        }
    }

    /**
     * \brief destructor
     */
    ~Buffer(){
        clean();
    }

    /**
     * \brief set each bytes of _data to (c) with length of (size)
     */
    SaberStatus mem_set(int c, size_t size) {
    	if(!_own_data|| _count < size){
            return SaberOutOfAuthority;
        }
        API::mem_set(_data, c, size);
    	return SaberSuccess;
    }

    /**
     * \brief re-alloc memory, only if hold the data, can be relloc
     */
    SaberStatus re_alloc(size_t size){
        if (size > _capacity){
            if (_own_data) {
                CHECK_EQ(_id, API::get_device_id()) << \
                    "buffer is not declared in current device, could not re_alloc buffer";
                clean();
                API::mem_alloc(&_data, size);
                _capacity = size;
            } else {
                return SaberOutOfAuthority;
            }
        }
        _count = size;
        return SaberSuccess;
    }

    /**
     * \brief free old memory, alloc new memory
     */
    SaberStatus alloc(size_t size){
        clean();
        API::mem_alloc(&_data, size);
        _capacity = size;
        _own_data = true;
        _count = size;
        return SaberSuccess;
    }

    /**
     * \brief
     */
    int get_id() const {
        return _id;
    }

    /**
     * \brief synchronously copy from other Buf
     */
    template <TargetType TType_t>
    SaberStatus sync_copy_from(Buffer<TType_t>& buf){
        CHECK_GE(_capacity, buf.get_count());
        typedef  TargetWrapper<TType_t> API_t;
        typedef typename TargetTypeTraits<TType>::target_category target_category;
        typedef typename TargetTypeTraits<TType>::target_type target_type_this;
        typedef typename TargetTypeTraits<TType_t>::target_type target_type_t;
        typedef typename IF<std::is_same<target_type_this, target_type_t>::value, __HtoH, __DtoH>::Type then_type;
        typedef typename IF<std::is_same<target_type_this, target_type_t>::value, __DtoD, __HtoD>::Type else_type;
        typedef typename IF<std::is_same<target_category, __host_target>::value, then_type, else_type>::Type flag_type;

        typedef typename IF<std::is_same<target_category , __host_target>::value, API_t, API>::Type process_API;

        LOG(INFO) << "sync memcpy h2h, size: " << buf.get_count();

        process_API::sync_memcpy(_data, 0, _id, buf.get_data(), \
            0, buf.get_id(), buf.get_count(), flag_type());

        return SaberSuccess;
    }

    /**
     * \brief return const data pointer
     */
    const TPtr get_data(){
        return _data;
    }

    /**
     * \brief return mutable data pointer
     */
    TPtr get_data_mutable(){
        return _data;
    }

    /**
     * \brief return current size of memory, in size
     */
    inline size_t get_count() const { return _count;}

    /**
     * \brief return total size of memory, in size
     */
    inline size_t get_capacity() const { return _capacity; }

private:
    //! \brief device id where data allocated
    int _id;
    TPtr _data;
    bool _own_data;
    size_t _count;
    size_t _capacity;

    /**
     * \brief free memory
     */
    SaberStatus clean(){
        if (_own_data && _capacity > 0) {
            _count = 0;
            _capacity = 0;
            _own_data = true;
            API::mem_free(_data);
        }
        _data = nullptr;
        return SaberSuccess;
    }
};

template <TargetType TType_dst, TargetType TType_src>
static inline int MemShare(std::shared_ptr<Buffer<TType_dst>>& dst, \
    const std::shared_ptr<Buffer<TType_src>>& src, __DtoD) {
    //LOG(INFO) << "shared D2D";
    if(dst->get_id() == src->get_id()){
        dst = src;
        return 1;
    }
    //LOG(INFO) << "copied D2D";
    ICESWORD_CHECK(dst->re_alloc(src->get_count()));
    ICESWORD_CHECK(dst->sync_copy_from(*src));
    return 0;
}

template <TargetType TType_dst, TargetType TType_src>
static inline int MemShare(std::shared_ptr<Buffer<TType_dst>>& dst, \
    const std::shared_ptr<Buffer<TType_src>>& src, __HtoD) {
    //LOG(INFO) << "copied H2D";
    ICESWORD_CHECK(dst->re_alloc(src->get_count()));
    ICESWORD_CHECK(dst->sync_copy_from(*src));
    return 0;
}

template <TargetType TType_dst, TargetType TType_src>
static inline int MemShare(std::shared_ptr<Buffer<TType_dst>>& dst, \
    const std::shared_ptr<Buffer<TType_src>>& src, __HtoH) {
    //LOG(INFO) << "shared H2H";
    dst = src;
    return 1;
}

template <TargetType TType_dst, TargetType TType_src>
static inline int MemShare(std::shared_ptr<Buffer<TType_dst>>& dst, \
    const std::shared_ptr<Buffer<TType_src>>& src, __DtoH) {
    //LOG(INFO) << "copied D2H";
    ICESWORD_CHECK(dst->re_alloc(src->get_count()));
    ICESWORD_CHECK(dst->sync_copy_from(*src));
    return 0;
}

template <TargetType TType_dst, TargetType TType_src>
static inline int BufferMemShare(std::shared_ptr<Buffer<TType_dst>>& dst, \
    const std::shared_ptr<Buffer<TType_src>>& src){

    typedef typename TargetTypeTraits<TType_dst>::target_type target_type_dst;
    typedef typename TargetTypeTraits<TType_src>::target_type target_type_src;
    typedef typename TargetTypeTraits<TType_dst>::target_category target_category_dst;

    typedef typename IF<std::is_same<target_type_dst, target_type_src>::value, __HtoH, __DtoH>::Type then_type;
    typedef typename IF<std::is_same<target_type_dst, target_type_src>::value, __DtoD, __HtoD>::Type else_type;
    typedef typename IF<std::is_same<target_category_dst, __host_target>::value, then_type, else_type>::Type flag_type;
            CHECK_EQ(src == nullptr, false) << "input buffer is null!";
    if (!dst){
        dst = std::make_shared<Buffer<TType_dst>>(src->get_count());
    }
    return MemShare(dst, src, flag_type());
}


} //namespace icesword

} //namespace noobsdnn

#endif //NBDNN_ICESWORD_CORE_BUFFER_H


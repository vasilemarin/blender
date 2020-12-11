
// cyCodeBase by Cem Yuksel
// [www.cemyuksel.com]
//-------------------------------------------------------------------------------
//! \file   cyHeap.h
//! \author Cem Yuksel
//!
//! \brief  A general-purpose heap class
//!
//! This file includes a general-purpose heap class.
//!
//-------------------------------------------------------------------------------
//
// Copyright (c) 2016, Cem Yuksel <cem@cemyuksel.com>
// All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//-------------------------------------------------------------------------------
#pragma warning(push, 0)

#ifndef _CY_HEAP_H_INCLUDED_
#  define _CY_HEAP_H_INCLUDED_

//-------------------------------------------------------------------------------

#  include <assert.h>
#  include <stdint.h>

//-------------------------------------------------------------------------------
namespace cy {
//-------------------------------------------------------------------------------

//! A general-purpose max-heap structure that allows random access and updates.
//!
//! The main data can be kept in an external array or within the Heap class.

class Heap {
 public:
  //////////////////////////////////////////////////////////////////////////!//!//!
  //!@name Constructor and Destructor

  Heap()
      : size(0),
        heapItemCount(0),
        data(nullptr),
        heap(nullptr),
        heapPos(nullptr),
        deleteData(false)
  {
  }
  ~Heap()
  {
    Clear();
  }

  //////////////////////////////////////////////////////////////////////////!//!//!
  //!@name Initialization methods

  //! Deletes all data owned by the class.
  void Clear()
  {
    ClearData();
    ClearHeap();
  }

  //! Copies the main data items from an array into the internal storage of this class.
  void CopyData(float const *items, size_t itemCount)
  {
    ClearData();
    size = itemCount;
    data = new float[size];
    for (size_t i = 0; i < size; i++)
      data[i] = items[i];
    deleteData = true;
  }

  //! Moves the main data items from an array to the internal storage of this class.
  //! Modifying this array externally can invalidate the heap structure.
  //! The given array must NOT be deleted externally.
  //! The given items pointer still points to the same data, but the class claims
  //! ownership of the data. Therefore, when the class object is deleted, the data
  //! items are deleted as well. If this is not desirable, use SetDataPointer.
  void MoveData(float *items, size_t itemCount)
  {
    ClearData();
    data = items;
    size = itemCount;
    deleteData = true;
  }

  //! Sets the data pointer of this class. This method is used for sharing the items
  //! array with other structures. Unlike setting the main data using the MoveData
  //! method, when SetDataPointer is used, the class does NOT claim ownership
  //! of the data. Therefore, it does not deallocate memory used for the main data
  //! when it is deleted, and the data items must be deleted externally.
  //! However, the data items must NOT be deleted while an object of this class is used.
  void SetDataPointer(float *items, size_t itemCount)
  {
    ClearData();
    data = items;
    size = itemCount;
    deleteData = false;
  }

  //! The Build method builds the heap structure using the main data. Therefore,
  //! the main data must be set using either CopyData, MoveData, or SetDataPointer
  //! before calling the Build method.
  void Build()
  {
    ClearHeap();
    heapItemCount = size;
    heap = new size_t[size + 1];
    heapPos = new size_t[size];
    for (size_t i = 0; i < heapItemCount; i++)
      heapPos[i] = i + 1;
    for (size_t i = 1; i <= heapItemCount; i++)
      heap[i] = i - 1;
    if (heapItemCount <= 1)
      return;
    for (size_t ix = heapItemCount / 2; ix > 0; ix--)
      HeapMoveDown(ix);
  }

  //////////////////////////////////////////////////////////////////////////!//!//!
  //!@name Access and manipulation methods

  //! Returns the item from the main data with the given id.
  float const &GetItem(size_t id) const
  {
    assert(id < size);
    return data[id];
  }

  //! Sets the item with the given id and updates the heap structure accordingly.
  //! Returns false if the item is not in the heap anymore (removed by Pop) or if its heap position
  //! is not changed.
  bool SetItem(size_t id, float const &item)
  {
    assert(id < size);
    data[id] = item;
    return MoveItem(id);
  }

  //! Moves the item with the given id to the correct position in the heap.
  //! This method is useful for fixing the heap position after an item is modified externally.
  //! Returns false if the item is not in the heap anymore (removed by Pop) or if its heap position
  //! is not changed.
  bool MoveItem(size_t id)
  {
    return HeapOrder(heapPos[id]);
  }

  //! Moves the item with the given id towards the top of the heap.
  //! This method is useful for fixing the heap position after an item is modified externally to
  //! increase its priority. Returns false if the item is not in the heap anymore (removed by Pop)
  //! or if its heap position is not changed.
  bool MoveItemUp(size_t id)
  {
    return HeapMoveUp(heapPos[id]);
  }

  //! Moves the item with the given id towards the top of the heap.
  //! This method is useful for fixing the heap position after an item is modified externally to
  //! decrease its priority. Returns false if the item is not in the heap anymore (removed by Pop)
  //! or if its heap position is not changed.
  bool MoveItemDown(size_t id)
  {
    return HeapMoveDown(heapPos[id]);
  }

  //! Returns if the item with the given id is in the heap or removed by Pop.
  bool IsInHeap(size_t id) const
  {
    assert(id < size);
    return heapPos[id] <= heapItemCount;
  }

  //! Returns the number of items in the heap.
  size_t NumItemsInHeap() const
  {
    return heapItemCount;
  }

  //! Returns the item from the heap with the given heap position.
  //! Note that items that are removed from the heap appear in the inverse order
  //! with which they were removed after the last item in the heap.
  float const &GetFromHeap(size_t heapIndex) const
  {
    assert(heapIndex < size);
    return data[heap[heapIndex + 1]];
  }

  //! Returns the id of the item from the heap with the given heap position.
  //! Note that items that are removed from the heap appear in the inverse order
  //! with which they were removed after the last item in the heap.
  size_t GetIDFromHeap(size_t heapIndex) const
  {
    assert(heapIndex < size);
    return heap[heapIndex + 1];
  }

  //! Returns the item at the top of the heap.
  float const &GetTopItem() const
  {
    assert(size >= 1);
    return data[heap[1]];
  }

  //! Returns the id of the item at the top of the heap.
  size_t GetTopItemID() const
  {
    assert(size >= 1);
    return heap[1];
  }

  //! Removes and returns the item at the top of the heap.
  //! The removed item is not deleted, but it is removed from the heap
  //! by placing it right after the last item in the heap.
  void Pop(float &item)
  {
    Pop();
    item = data[heap[heapItemCount]];
  }

  //! Removes the item at the top of the heap.
  //! The removed item is not deleted, but it is removed from the heap
  //! by placing it right after the last item in the heap.
  void Pop()
  {
    SwapItems(1, heapItemCount);
    heapItemCount--;
    HeapMoveDown(1);
  }

 private:
  //////////////////////////////////////////////////////////////////////////!//!//!
  //!@name Internal structures and methods

  float *data;           // The main data pointer.
  size_t *heap;          // The heap array, keeping the id of each data item.
  size_t *heapPos;       // The heap position of each item.
  size_t heapItemCount;  // The number of items in the heap.
  size_t size;           // The total item count, including the ones removed from the heap.
  bool deleteData;       // Determines whether the data pointer owns the memory it points to.

  // Clears the data pointer and deallocates memory if the data is owned.
  void ClearData()
  {
    if (deleteData)
      delete[] data;
    data = nullptr;
    deleteData = false;
    size = 0;
  }

  // Clears the heap structure.
  void ClearHeap()
  {
    delete[] heap;
    heap = nullptr;
    delete[] heapPos;
    heapPos = nullptr;
    heapItemCount = 0;
  }

  // Checks if the item should be moved.
  // Returns true if the item is in the heap.
  bool HeapOrder(size_t ix)
  {
    if (ix > heapItemCount)
      return false;
    if (HeapMoveUp(ix))
      return true;
    return HeapMoveDown(ix);  // if it can't be move up, move it down
  }

  // Checks if the item should be moved up, returns true if the item is moved.
  bool HeapMoveUp(size_t ix)
  {
    size_t org = ix;
    while (ix >= 2) {
      size_t parent = ix / 2;
      if (!IsSmaller(parent, ix))
        break;
      SwapItems(parent, ix);
      ix = parent;
    }
    return ix != org;
  }

  // Checks if the item should be moved down, returns true if the item is moved.
  bool HeapMoveDown(size_t ix)
  {
    size_t org = ix;
    size_t child = ix * 2;
    while (child + 1 <= heapItemCount) {
      if (IsSmaller(child, child + 1))
        child++;
      if (!IsSmaller(ix, child))
        return ix != org;
      SwapItems(ix, child);
      ix = child;
      child = ix * 2;
    }
    // Check the very last item as well
    if (child <= heapItemCount) {
      if (IsSmaller(ix, child)) {
        SwapItems(ix, child);
        return true;
      }
    }
    return ix != org;
  }

  // Returns if the item at ix1 is smaller than the one at ix2.
  bool IsSmaller(size_t ix1, size_t ix2)
  {
    return data[heap[ix1]] < data[heap[ix2]];
  }

  // Swaps the heap positions of items at ix1 and ix2.
  void SwapItems(size_t ix1, size_t ix2)
  {
    size_t t = heap[ix1];
    heap[ix1] = heap[ix2];
    heap[ix2] = t;
    heapPos[heap[ix1]] = ix1;
    heapPos[heap[ix2]] = ix2;
  }

  //////////////////////////////////////////////////////////////////////////!//!//!
};

//-------------------------------------------------------------------------------
}  // namespace cy
//-------------------------------------------------------------------------------

#endif

#pragma warning(pop)

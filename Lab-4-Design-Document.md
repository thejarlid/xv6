# Lab 4 Design Document

## Overview

Since our machine has limited physical memory, to allow applications to consume more memory than the limitations of physical memory, our operating system needs to facilitate a method of page swap. 

The general idea is to designate a region of space on disk that we call the swap space and then we can move pages from physical memory to disk and back seamlessly to achieve the hopefully “un-noticably” larger amount of memory. 

The goal of this lab is to simply implement 1 feature, LRU. LRU stands for Least Recently Used which describes the page that we are going to evict. We want to evict the page from physical memory that was used the farthest back in hopes that this predicts the future well. 

## In-Depth Analysis and Implementation

To implement this feature we will split the project into 3 parts:

1. Add swap space segment to disk
2. Swap pages in and out (act as if we have more memory)
3. LRU

******Swap Space Region**
To begin we want to add the swap space segment into the disk and to do this we will have to modify mkfs.c, this file handles segmentation of the disk. Our goal is to insert a swap space region between the superblock and the bitmap. 

We want to be able to allocate an additional 8192 pages, since the disk is managed in blocks which are 512 bytes we have to use 8 blocks per page. To begin we will add 2 fields to the superblock struct which will represent the start and size of the swap region. We then assign these in the main method for mkfs. We start the swap space at 2 which is the previous start of the bitmap. Then we shift segment in the disk up by the swap size which is 8 * 8192.

**Swap Pages in and Out**
We start by creating a new struct and array to replicate the core map but for the disk. The swap_core_map will be statically allocated with 8192 elements, one for each page on disk. The array will also be of type struct swap_map_entry. This entry will have 4 fields, pid, va, refCount, inUse. The first 3 fields are to copy the data from a core_map_entry so they are identical. The last fields is a variable that I use to cycle through the swap_core_map to find an element that is not in use and to which I can evict onto disk. 

We begin by first evicting a page when we cannot kalloc anymore and the high level idea is to evict a page from physical memory, Free the page to put it back onto the freelist and then kalloc again knowing that a page will be there. To achieve this we follow these steps

  1. cycle through core map
  2. loop till we find the first core map entry that is a non-kernel page.
  3. find a space in the swap region to put the physical page by cycling through the swap_core_map until we find an index where the in_use field is 0
  4. Once found move data block by block from physical page to disk page at index * BSIZE + offset
  5. Copy values of the core_map_entry we are evicting into the swap_map_entry and set the inUse bit to 1.
  6. Loop over all processes in the ptable use walkpml4 to get a pte for the va of the core_map_entry
    1. see if the physical memory address matches the physical address we are swapping out
    2. For each valid pte we need to indicate that the page is on disk and turn off the present bit

To swap pages in we must first trap into the kernel which will happen because the present bit is off. Then we have a method which will work under the assumption that it is ok to move a page into physical memory because there is a newly evicted spot. 

The swap in algorithm runs similar to swapping out but we get the swap_core_map index that the page we need to bring in by using the PTE number that replace on eviction to hold the swap_map index. We then kalloc a page, move memory byte by byte back into the newly kalloced page. We then add a physical mem map between the virtual address we are accessing and the page we have now allocated. Then we copy our swap_core_map data into the core_map_entry for the kalloced page. Finally we cycle through the ptable to tell all processes that access the same disk page that the page is now in memory and update the pte to appropriately reference that and then we can remove the inUse bit from our swap_map_entry. 

**LRU**

For LRU we change the above code’s eviction to instead simply find a page by using the clock algorithm. We will cycle through the core_map and for each page see whether the A bit is turned off if it is we use it, otherwise we turn off the bit and move to the next element. We continue looping over the entire swap_core_map till we find one page with the A bit off.

## Risk Analysis

This is probably gonna take a while we’re looking at around 50 hours of work and 300 lines of code. We will also have a ton of bugs that I’ll have to fix. 


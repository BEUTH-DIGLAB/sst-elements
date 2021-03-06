// Copyright 2009-2017 Sandia Corporation. Under the terms
// of Contract DE-NA0003525 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2017, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.
//


/* Author: Amro Awad
 * E-mail: amro.awad@ucf.edu
 */
/* Author: Vamsee Reddy Kommareddy
 * E-mail: vamseereddy@knights.ucf.edu
 */

#include<list>
#include<map>
#include<cmath>

#include "Opal_Event.h"

typedef struct mpr{
	long long int pAddress;
	int num_frames;
	int frame_size;
}MemPoolResponse;

// This defines a physical frame of size 4KB by default
class Frame{

	public:
		// Constructor
		Frame() { starting_address = 0; metadata = 0;}

		// Constructor with paramteres
		Frame(long long int st, long long int md) { starting_address = st; metadata = 0;}

		// The starting address of the frame
		long long int starting_address;

		// This will be used to store information about current allocation
		int metadata;

};


// This class defines a memory pool 

class Pool{

	public:

		//Constructor for pool
		Pool(Params parmas);

		// The size of the memory pool in KBs
		long long int size; 

		// The starting address of the memory pool
		long long int start;

		// Allocate N contigiuous frames, returns the starting address if successfull, or -1 if it fails!
		long long int allocate_frame(int N);

		// Allocate 'size' contigiuous memory, returns a structure with starting address and number of frames allocated
		MemPoolResponse allocate_frames(int size);

		// Freeing N frames starting from Address X, this will return -1 if we find that these frames were not allocated
		int deallocate_frame(long long int X, int N);

		// Deallocate 'size' contigiuous memory starting from physical address 'starting_pAddress', returns a structure which indicates success or not
		MemPoolResponse deallocate_frames(int size, long long int starting_pAddress);

		// Current number of free frames
		int freeframes() { return freelist.size(); }

		// Frame size in KBs
		int frsize;

		//Total number of frames
		int num_frames;

		//real size of the memory pool
		long long int real_size;

		//number of free frames
		int available_frames;

		void set_memPool_type(SST::OpalComponent::MemType _memType) { memType = _memType; }

		SST::OpalComponent::MemType get_memPool_type() { return memType; }

		void set_memPool_tech(SST::OpalComponent::MemTech _memTech) { memTech = _memTech; }

		SST::OpalComponent::MemTech get_memPool_tech() { return memTech; }

		void build_mem();

	private:

		Output *output;

		//shared or local
		SST::OpalComponent::MemType memType;

		//Memory technology
		SST::OpalComponent::MemTech memTech;

		// The list of free frames
		std::list<Frame*> freelist;

		// The list of allocated frames --- the key is the starting physical address
		std::map<long long int, Frame*> alloclist;


};


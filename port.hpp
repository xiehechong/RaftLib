/**
 * port.hpp - 
 * @author: Jonathan Beard
 * @version: Thu Aug 28 09:55:47 2014
 * 
 * Copyright 2014 Jonathan Beard
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _PORT_HPP_
#define _PORT_HPP_  1

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <typeinfo>
#include <typeindex>
#include <functional>
#include <utility>

#include "portbase.hpp"
#include "graphtools.hpp"
#include "ringbuffertypes.hpp"
#include "fifo.hpp"
#include "port_info.hpp"
#include "ringbuffer.tcc"
#include "port_info_types.hpp"
#include "portmap_t.hpp"
#include "portiterator.hpp"

/** needed for friending below **/
class MapBase;

/** need to pre-declare this **/
namespace raft{
   class kernel;
}

class Port : public PortBase
{
public:
   /** 
    * Port - constructor used to construct a standard port
    * object, needs a reference to the parent kernel for
    * the port_info struct
    * @param   k  - raft::kernel*
    */
   Port( raft::kernel *k );
   
   /**
    * Port - constructor used to construct a port with 
    * pre-allocated memory, useful for things like 
    * array distribution and reduction
    * @param   k - raft::kernel*
    * @param   ptr - void*
    * @param   nbytes - const std::size_t length in bytes
    */
   Port( raft::kernel *k, void * const ptr, const std::size_t nbytes );
   /**
    * ~Port - destructor, deletes the FIFO that was given
    * when the object was initalized.
    */
   virtual ~Port();

   /**
    * addPort - adds and initializes a port for the name
    * given.  Function returns true if added, false if not.
    * Main reason for returning false would be that the 
    * port already exists.
    * @param   port_name - const std::string
    * @return  bool
    */
   template < class T >
   bool addPort( const std::string port_name )
   {
      /**
       * we'll have to make a port info object first and pass it by copy
       * to the portmap.  Perhaps re-work later with pointers, but for
       * right now this will work and it doesn't necessarily have to
       * be performant since its only executed once.
       */
       PortInfo pi( typeid( T ) );
       pi.my_kernel = kernel;
       pi.my_name   = port_name;
       (this)->initializeConstMap<T>( pi );
      const auto ret_val( portmap.map.insert( std::make_pair( port_name, 
                                                          pi ) ) );
      return( ret_val.second );
   }

   /** 
    * addPorts - add ports for an existing buffer, basically
    * allocate buffers in place.  These also won't be able
    * to be resized.  
    * @param n_ports - const std::size_t 
    */
   template < class T >
   bool addPorts( const std::size_t n_ports = 0 )
   {
      T *existing_buff_t( reinterpret_cast< T* >( alloc_ptr ) );
      std::size_t length( alloc_ptr_length / sizeof( T ) );
      const std::size_t inc( length / n_ports );
      const std::size_t adder( length % n_ports );

      for( std::size_t index( 0 ); index < n_ports; index++ )
      {
         const std::size_t start_index( index * inc );
         PortInfo pi( typeid( T ), 
                      (void*)&( existing_buff_t[ start_index ] ) /** pointer **/,
                      inc + ( index == (n_ports - 1) ? adder : 0 ),
                      start_index );
         pi.my_kernel = kernel;
         const std::string name( std::to_string( index ) );
         pi.my_name   = name;
         (this)->initializeConstMap< T >( pi );
         portmap.map.insert( std::make_pair( name, pi ) );
      }
      return( true );
   }

   /**
    * getPortType - input the port name, and get the hash
    * for the type of the port.  This function is useful
    * for checking the streaming graph to make sure all the
    * ports that are "dynamically" created do in fact have
    * compatible types.
    * @param port_name - const std::string
    * @return  const type_index&
    * @throws PortNotFoundException
    */
   const std::type_index& getPortType( const std::string port_name );


   /**
    * operator[] - input the port name and get a port
    * if it exists. 
    */
   virtual FIFO& operator[]( const std::string port_name );


   /**
    * hasPorts - returns true if any ports exists, false
    * otherwise. 
    * @return   bool
    */
   virtual bool hasPorts();
  
   /**
    * begin - get the beginning port.
    * @return PortIterator
    */
   virtual PortIterator begin();

   /**
    * end - get the end port 
    * @return PortIterator
    */
   virtual PortIterator end();
   
   /**
    * count - get the total number of fifos within this port container
    * @return std::size_t
    */
   std::size_t count();

protected:
   /**
    * initializeConstMap - hack to get around the inability to otherwise
    * initialize a template function where later we don't have the 
    * template parameter.  NOTE:  this is a biggy, if we have more 
    * FIFO types in the future (i.e., sub-classes of FIFO) then we
    * must create an entry here otherwise bad things will happen.
    * @param   pi - PortInfo&
    */
   template < class T > void initializeConstMap( PortInfo &pi )
   {
      pi.const_map.insert(  
         std::make_pair( Type::Heap , new instr_map_t() ) );
      
      pi.const_map[ Type::Heap ]->insert(
         std::make_pair( false /** no instrumentation **/,
                         RingBuffer< T, Type::Heap, false >::make_new_fifo ) );
      pi.const_map[ Type::Heap ]->insert(
         std::make_pair( true /** yes instrumentation **/,
                         RingBuffer< T, Type::Heap, true >::make_new_fifo ) );

      pi.const_map.insert( std::make_pair( Type::SharedMemory, new instr_map_t() ) );
      pi.const_map[ Type::SharedMemory ]->insert(
         std::make_pair( false /** no instrumentation **/,
                         RingBuffer< T, Type::SharedMemory >::make_new_fifo ) );
      /** no instrumentation version defined yet **/
      return;
   }
   
   /**
    * getPortInfo - returns the PortInfo struct for a kernel if we
    * expect it to have a single port.  If there's more than one port
    * this function throws an exception.
    * @return  std::pair< std::string, PortInfo& >
    */
   PortInfo& getPortInfo();

   /** 
    * getPortInfoFor - gets port information for the param port
    * throws an exception if the port doesn't exist. 
    * @param   port_name - const std::string
    * @return  PortInfo&
    */
   PortInfo& getPortInfoFor( const std::string port_name );
 
   /**
    * portmap - container struct with all ports.  The 
    * mutex should be locked before accessing this structure
    */
   portmap_t   portmap;

   /** 
    * parent kernel that owns this port 
    */
   raft::kernel *    kernel            = nullptr;
    
   /**
    * ptr used for in-place allocations, will
    * not be deleted by the map, also should not
    * be modified by the map either.
    */
   void * const      alloc_ptr         = nullptr;
   
   /**
    * alloc_ptr_length - length of alloc_ptr in 
    * bytes.
    */
   const std::size_t alloc_ptr_length  = 0;
   
   /** we need some friends **/
   friend class MapBase;
   friend void GraphTools::BFS( std::set< raft::kernel* > &source_kernels,
                                edge_func fun,
                                bool connection_error );
   
};
#endif /* END _PORT_HPP_ */

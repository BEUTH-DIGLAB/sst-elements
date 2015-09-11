// Copyright 2009-2015 Sandia Corporation. Under the terms
// of Contract DE-AC04-94AL85000 with Sandia Corporation, the U.S.
// Government retains certain rights in this software.
// 
// Copyright (c) 2009-2015, Sandia Corporation
// All rights reserved.
// 
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef COMPONENTS_FIREFLY_CTRLMSGPROCESSQUEUESSTATE_H
#define COMPONENTS_FIREFLY_CTRLMSGPROCESSQUEUESSTATE_H

#include <sst/core/output.h>
#include "ctrlMsgState.h"
#include "ctrlMsgXXX.h"

namespace SST {
namespace Firefly {
namespace CtrlMsg {

template< class T1 >
class ProcessQueuesState : StateBase< T1 > 
{
    static const unsigned long MaxPostedShortBuffers = 512;
    static const unsigned long MinPostedShortBuffers = 5;
    typedef typename T1::Callback Callback;
    typedef typename T1::Callback2 Callback2;

  public:
    ProcessQueuesState( int verbose, int mask, T1& obj ) : 
        StateBase<T1>( verbose, mask, obj ),
        m_getKey( 0 ),
        m_rspKey( 0 ),
        m_needRecv( 0 ),
        m_numNicRequestedShortBuff(0),
        m_numRecvLooped(0),
        m_missedInt( false ),
		m_intCtx(NULL)
    {
        char buffer[100];
        snprintf(buffer,100,"@t:%#x:%d:CtrlMsg::ProcessQueuesState::@p():@l ",
                            obj.nic().getNodeId(), obj.info()->worldRank());
        dbg().setPrefix(buffer);
        for ( unsigned long i = 0; i < MinPostedShortBuffers; i++ ) {
            postShortRecvBuffer();
        }
    }

    void finish() {
        dbg().verbose(CALL_INFO,1,0,"pstdRcvQ=%lu recvdMsgQ=%lu loopResp=%lu %lu\n", 
             m_pstdRcvQ.size(), m_recvdMsgQ.size(), m_loopResp.size(), m_funcStack.size() );
    }

    ~ProcessQueuesState() 
    {
        while ( ! m_postedShortBuffers.empty() ) {
            delete m_postedShortBuffers.begin()->first;
            delete m_postedShortBuffers.begin()->second;
            m_postedShortBuffers.erase( m_postedShortBuffers.begin() );
        }
    }

    void enterSend( _CommReq*, FunctorBase_0<bool>* func );
    void enterRecv( _CommReq*, FunctorBase_0<bool>* func );
    void enterWait( WaitReq*, FunctorBase_0<bool>* func );

    void needRecv( int, int, size_t );

    void loopHandler( int, std::vector<IoVec>&, void* );
    void loopHandler( int, void* );

  private:

    class Msg {
      public:
        Msg( MatchHdr* hdr ) : m_hdr( hdr ) {}
        virtual ~Msg() {}
        MatchHdr& hdr() { return *m_hdr; }
        std::vector<IoVec>& ioVec() { return m_ioVec; }

      protected:
        std::vector<IoVec> m_ioVec;

      private:
        MatchHdr* m_hdr;
    };

    class ShortRecvBuffer : public Msg {
      public:
        ShortRecvBuffer(size_t length ) : Msg( &hdr ) 
        { 
            ioVec.resize(2);    

            ioVec[0].ptr = &hdr;
            ioVec[0].len = sizeof(hdr);

            if ( length ) {
                buf.resize( length );
            	ioVec[1].ptr = &buf[0];
            } else {
            	ioVec[1].ptr = NULL; 
			}
            ioVec[1].len = length;

            ProcessQueuesState<T1>::Msg::m_ioVec.push_back( ioVec[1] ); 
        }

        MatchHdr                hdr;
        std::vector<IoVec>      ioVec; 
        std::vector<unsigned char> buf;
    };

    struct LoopReq : public Msg {
        LoopReq(int _srcCore, std::vector<IoVec>& _vec, void* _key ) :
            Msg( (MatchHdr*)_vec[0].ptr ), 
            srcCore( _srcCore ), vec(_vec), key( _key) 
        {
            ProcessQueuesState<T1>::Msg::m_ioVec.push_back( vec[1] ); 
        }

        int srcCore;
        std::vector<IoVec>& vec;
        void* key;
    };

    struct LoopResp{
        LoopResp( int _srcCore, void* _key ) : srcCore(_srcCore), key(_key) {}
        int srcCore;
        void* key;
    };

    struct GetInfo {
        _CommReq*  req;
        CtrlHdr    hdr;
        nid_t      nid;
        uint32_t   tag;
        size_t     length;
    };

    class FuncCtxBase {
      public:
        FuncCtxBase( Callback ret = NULL ) : 
			callback(ret) {}

		virtual ~FuncCtxBase() {}
        Callback getCallback() {
			return callback;
		} 

		void setCallback( Callback arg ) {
			callback = arg;
		}

	  private:
        Callback callback;
    };

    class ProcessQueuesCtx : public FuncCtxBase {
	  public:
		ProcessQueuesCtx( Callback callback ) :
			FuncCtxBase( callback ) {}
    };
	
    class InterruptCtx : public FuncCtxBase {
	  public:
		InterruptCtx( Callback callback ) :
			FuncCtxBase( callback ) {}
    };

    class ProcessShortListCtx : public FuncCtxBase {
      public:
        ProcessShortListCtx( std::deque<Msg*>& q ) : 
            m_msgQ(q), m_iter(m_msgQ.begin()) { }

        MatchHdr&   hdr() {
            return (*m_iter)->hdr();
        }

        std::vector<IoVec>& ioVec() { 
     	 	return (*m_iter)->ioVec();
        }

        Msg* msg() {
            return *m_iter;
        }
        std::deque<Msg*>& getMsgQ() { return m_msgQ; }
        bool msgQempty() { return m_msgQ.empty(); } 

        _CommReq*    req; 

        void removeMsg() { 
            delete *m_iter;
            m_iter = m_msgQ.erase(m_iter);
        }

        bool isDone() { return m_iter == m_msgQ.end(); }
        void incPos() { ++m_iter; }
      private:
        std::deque<Msg*> 			m_msgQ; 
        typename std::deque<Msg*>::iterator 	m_iter;
    };

    class WaitCtx : public FuncCtxBase {
      public:
		WaitCtx( WaitReq* _req, Callback callback ) :
			FuncCtxBase( callback ), req( _req ) {}

        ~WaitCtx() { delete req; }

        WaitReq*    req;
    };

    class ProcessLongGetFiniCtx : public FuncCtxBase {
      public:
		ProcessLongGetFiniCtx( _CommReq* _req ) : req( _req ) {}

        ~ProcessLongGetFiniCtx() { }

        _CommReq*    req;
    };


    nid_t calcNid( _CommReq* req, MP::RankID rank ) {
        return obj().info()->getGroup( req->getGroup() )->getMapping( rank );
    }

    MP::RankID getMyRank( _CommReq* req ) {
        return obj().info()->getGroup( req->getGroup() )->getMyRank();
    } 


    void processLoopResp( LoopResp* );
    void processLongAck( GetInfo* );
    void processLongGetFini( std::deque< FuncCtxBase* >&, _CommReq* );
    void processLongGetFini0( std::deque< FuncCtxBase* >* );
    void processPioSendFini( _CommReq* );

    void processSend( _CommReq* );
    void processRecv( _CommReq* );
    void processSendLoop( _CommReq* );
    void enterWait0( std::deque< FuncCtxBase* >* );
    void processQueues( std::deque< FuncCtxBase* >& );
    void processQueues0( std::deque< FuncCtxBase* >* );
    void processShortList( std::deque< FuncCtxBase* >& );
    void processShortList0( std::deque< FuncCtxBase* >* );
    void processShortList1( std::deque< FuncCtxBase* >* );
    void processShortList2( std::deque< FuncCtxBase* >* );

	void enableInt( FuncCtxBase*, 
			void (ProcessQueuesState::*)( std::deque< FuncCtxBase* >*) );

    void foo();
    void foo0( std::deque< FuncCtxBase* >* );

    int copyIoVec( std::vector<IoVec>& dst, std::vector<IoVec>& src, size_t);

    Output& dbg()   { return StateBase<T1>::m_dbg; }
    T1& obj()       { return StateBase<T1>::obj; }

    void postShortRecvBuffer();

    void pioSendFiniVoid( void* );
    void pioSendFiniCtrlHdr( CtrlHdr* );
    void getFini( _CommReq* );
    void dmaRecvFiniGI( GetInfo*, nid_t, uint32_t, size_t );
    void dmaRecvFiniSRB( ShortRecvBuffer*, nid_t, uint32_t, size_t );

    bool        checkMatchHdr( MatchHdr& hdr, MatchHdr& wantHdr,
                                                    uint64_t ignore );
    _CommReq*	searchPostedRecv( MatchHdr& hdr, int& delay );
    void        print( char* buf, int len );

    void setExit( FunctorBase_0<bool>* exitFunctor ) {
        StateBase<T1>::setExit( exitFunctor );
    }

    FunctorBase_0<bool>* clearExit( ) {
        return StateBase<T1>::clearExit( );
    }

    void exit( int delay = 0 ) {
        StateBase<T1>::exit( delay );
    }

    key_t    genGetKey() {
        key_t tmp = m_getKey;;
        ++m_getKey;
        if ( m_getKey == LongGetKey ) m_getKey = 0; 
        return tmp | LongGetKey;
    }  

    key_t    genRspKey() {
        key_t tmp = m_rspKey;
        ++m_rspKey;
        if ( m_rspKey == LongRspKey ) m_rspKey = 0; 
        return tmp | LongRspKey;
    }  

    key_t    genReadReqKey( region_t region ) {
        //assert ( region to big ); 
        return region | ReadReqKey;
    }  

    key_t    genReadRspKey( region_t region ) {
        //assert ( region to big ); 
        return region | ReadRspKey;
    }  

    key_t   m_getKey;
    key_t   m_rspKey;
    int     m_needRecv;
    int     m_numNicRequestedShortBuff;
    int     m_numRecvLooped;
    bool    m_missedInt;

    std::deque< _CommReq* >         m_pstdRcvQ;
    std::deque< Msg* >              m_recvdMsgQ;

    std::deque< _CommReq* >         m_longGetFiniQ;
    std::deque< GetInfo* >          m_longAckQ;

    std::deque< FuncCtxBase* >      m_funcStack; 
    std::deque< FuncCtxBase* >      m_intStack; 

    std::deque< LoopResp* >         m_loopResp;

    std::map< ShortRecvBuffer*, Callback2* >
                                                    m_postedShortBuffers; 
	FuncCtxBase*	m_intCtx;
};

template< class T1 >
void ProcessQueuesState<T1>::enterSend( _CommReq* req,
                                        FunctorBase_0<bool>* exitFunctor )
{
    setExit( exitFunctor );
    size_t length = req->getLength( );
    int delay = obj().txDelay( length );

    dbg().verbose(CALL_INFO,2,1,"new send CommReq\n");

    req->setSrcRank( getMyRank( req ) );

    if ( obj().nic().isLocal( calcNid( req, req->getDestRank() ) ) ) {
        processSendLoop( req );
    } else {
        delay += obj().txNicDelay();

        delay += obj().txMemcpyDelay( sizeof( req->hdr() ) );
        if ( length > obj().shortMsgLength() ) {
            delay += obj().regRegionDelay( length );
        } else {
            delay += obj().txMemcpyDelay( length );
        }

        obj().schedCallback( 
            std::bind( &ProcessQueuesState<T1>::processSend, this, req ),
            delay
        );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::processSendLoop( _CommReq* req )
{
    dbg().verbose(CALL_INFO,2,2,"key=%p\n", req);

    IoVec hdrVec;
    hdrVec.ptr = &req->hdr();
    hdrVec.len = sizeof( req->hdr() );

    std::vector<IoVec> vec;
    vec.insert( vec.begin(), hdrVec );
    vec.insert( vec.begin() + 1, req->ioVec().begin(), 
                                        req->ioVec().end() );

    obj().loopSend( vec, obj().nic().calcCoreId( 
                        calcNid( req, req->getDestRank() ) ), req );

    if ( ! req->isBlocking() ) {
        exit();
    } else {
        enterWait( new WaitReq( req ), clearExit() );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::processSend( _CommReq* req )
{
    void* hdrPtr = NULL;
    size_t length = req->getLength( );

    IoVec hdrVec;   
    hdrVec.len = sizeof( req->hdr() ); 

    if ( length <= obj().shortMsgLength() ) {
        hdrPtr = hdrVec.ptr = malloc( hdrVec.len );
        memcpy( hdrVec.ptr, &req->hdr(), hdrVec.len );
    } else {
        hdrVec.ptr = &req->hdr(); 
    }

    std::vector<IoVec> vec;
    vec.insert( vec.begin(), hdrVec );
    
    nid_t nid = calcNid( req, req->getDestRank() );

    if ( length <= obj().shortMsgLength() ) {
        dbg().verbose(CALL_INFO,1,1,"Short %lu bytes dest %#x\n",length,nid); 
        vec.insert( vec.begin() + 1, req->ioVec().begin(), 
                                        req->ioVec().end() );
        req->setDone(obj().sendReqFiniDelay( length ));

    } else {
        dbg().verbose(CALL_INFO,1,1,"sending long message %lu bytes\n",length); 
        req->hdr().key = genGetKey();

        GetInfo* info = new GetInfo;

        Callback2* callback = new Callback2;
        *callback = std::bind( 
            &ProcessQueuesState<T1>::dmaRecvFiniGI, this, info, 
            std::placeholders::_1,
            std::placeholders::_2,
            std::placeholders::_3
        );

        info->req = req;

        std::vector<IoVec> hdrVec;
        hdrVec.resize(1);
        hdrVec[0].ptr = &info->hdr; 
        hdrVec[0].len = sizeof( info->hdr ); 
        
        obj().nic().dmaRecv( nid, req->hdr().key, hdrVec, callback ); 
        obj().nic().regMem( nid, req->hdr().key, req->ioVec(), NULL );
    }

    Callback* callback = new Callback;
    *callback = std::bind( &ProcessQueuesState::pioSendFiniVoid, this, hdrPtr );

    obj().nic().pioSend( nid, ShortMsgQ, vec, callback);

    if ( ! req->isBlocking() ) {
        exit();
    } else {
        enterWait( new WaitReq( req ), clearExit() );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::enterRecv( _CommReq* req,
                                        FunctorBase_0<bool>* exitFunctor )
{
    dbg().verbose(CALL_INFO,1,1,"new recv CommReq\n");
    setExit( exitFunctor );

    if ( m_postedShortBuffers.size() < MaxPostedShortBuffers ) {
        if ( m_numNicRequestedShortBuff ) {
            --m_numNicRequestedShortBuff; 
        } else if ( m_numRecvLooped ) {
            --m_numRecvLooped;
        } else {
            postShortRecvBuffer();
        }
    }

    m_pstdRcvQ.push_front( req );

    size_t length = req->getLength( );
    uint64_t delay = obj().rxPostDelay_ns( length );
    if ( length > obj().shortMsgLength() ) {
        delay += obj().regRegionDelay( length );
    }

    obj().schedCallback( 
        std::bind( &ProcessQueuesState::processRecv,this,req), 
        delay 
    );
}

template< class T1 >
void ProcessQueuesState<T1>::processRecv( _CommReq* req )
{
    if ( ! req->isBlocking() ) {
        exit();
    } else {
        enterWait( new WaitReq( req ), clearExit() );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::enterWait( WaitReq* req, 
                                    FunctorBase_0<bool>* exitFunctor  )
{
    dbg().verbose(CALL_INFO,1,1,"num pstd %lu, num short %lu\n",
                            m_pstdRcvQ.size(), m_recvdMsgQ.size() );

    setExit( exitFunctor );

    WaitCtx* ctx = new WaitCtx ( req,
        std::bind( &ProcessQueuesState<T1>::enterWait0, this, &m_funcStack ) 
    );

    m_funcStack.push_back( ctx );

    processQueues( m_funcStack );
}

template< class T1 >
void ProcessQueuesState<T1>::enterWait0( std::deque<FuncCtxBase*>* stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu\n", stack->size()); 
    dbg().verbose(CALL_INFO,1,1,"num pstd %lu, num short %lu\n",
                            m_pstdRcvQ.size(), m_recvdMsgQ.size() );
    WaitCtx* ctx = static_cast<WaitCtx*>( stack->back() );

    stack->pop_back();

	assert( stack->empty() );

    if ( ctx->req->isDone() ) {
        exit( ctx->req->getDelay() );
        delete ctx;
        ctx = NULL;
        dbg().verbose(CALL_INFO,2,1,"exit found CommReq\n"); 
    }

	if ( ctx ) {
		enableInt( ctx, &ProcessQueuesState::enterWait0 );
	}
}

template< class T1 >
void ProcessQueuesState<T1>::processQueues( std::deque< FuncCtxBase* >& stack )
{
    int delay = 0;
    dbg().verbose(CALL_INFO,2,1,"shortMsgV.size=%lu\n", m_recvdMsgQ.size() );
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu\n", stack.size()); 

    // this does not cost time
    while ( m_needRecv ) {
        ++m_numNicRequestedShortBuff; 
        postShortRecvBuffer();
        --m_needRecv;
    } 

    // this does not cost time
    while ( ! m_longAckQ.empty() ) {
        processLongAck( m_longAckQ.front() );
        m_longAckQ.pop_front();
    }

    // this does not cost time
    while ( ! m_loopResp.empty() ) {
        processLoopResp( m_loopResp.front() );
        m_loopResp.pop_front(); 
    }

    if ( ! m_longGetFiniQ.empty() ) {

        ProcessQueuesCtx* ctx = new ProcessQueuesCtx(
                bind( &ProcessQueuesState<T1>::processQueues0, this, &stack )  
        ); 
        stack.push_back( ctx );

        processLongGetFini( stack, m_longGetFiniQ.front() );

        m_longGetFiniQ.pop_front();

    } else if ( ! m_recvdMsgQ.empty() ) {

        ProcessQueuesCtx* ctx = new ProcessQueuesCtx(
                bind ( &ProcessQueuesState<T1>::processQueues0, this, &stack ) 
        );  
        stack.push_back( ctx );
        processShortList( stack );

    } else {
        obj().schedCallback( stack.back()->getCallback(), delay );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::processQueues0( std::deque< FuncCtxBase* >* stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu recvdMsgQ.size()=%lu\n", 
            stack->size(), m_recvdMsgQ.size() ); 
    
    delete stack->back();
    stack->pop_back();
    obj().schedCallback( stack->back()->getCallback() );
}

template< class T1 >
void ProcessQueuesState<T1>::processShortList( std::deque<FuncCtxBase*>& stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu recvdMsgQ.size()=%lu\n", 
            stack.size(), m_recvdMsgQ.size() ); 

    ProcessShortListCtx* ctx = new ProcessShortListCtx( m_recvdMsgQ );
	m_recvdMsgQ.clear();
    stack.push_back( ctx );

    processShortList0( &stack );
}

template< class T1 >
void ProcessQueuesState<T1>::processShortList0(std::deque<FuncCtxBase*>* stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu recvdMsgQ.size()=%lu\n", 
            stack->size(), m_recvdMsgQ.size() ); 

    ProcessShortListCtx* ctx = static_cast<ProcessShortListCtx*>( stack->back());
    
    int delay = 0;
    ctx->req = searchPostedRecv( ctx->hdr(), delay );

    if ( ctx->req ) {
        delay += obj().rxDelay( ctx->hdr().count * ctx->hdr().dtypeSize );

        if ( ! obj().nic().isLocal( calcNid( ctx->req, ctx->hdr().rank ) ) ) {
            delay += obj().rxNicDelay();
        }
    }

    obj().schedCallback( 
        std::bind( &ProcessQueuesState<T1>::processShortList1, this, stack ),
        delay 
    );
}

template< class T1 >
void ProcessQueuesState<T1>::processShortList1(std::deque<FuncCtxBase*>* stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu\n", stack->size()); 

    ProcessShortListCtx* ctx = static_cast<ProcessShortListCtx*>(stack->back());
    int delay = 0;

    if ( ctx->req ) {
        _CommReq* req = ctx->req;

        req->setResp( ctx->hdr().tag, ctx->hdr().rank, ctx->hdr().count );

        size_t length = ctx->hdr().count * ctx->hdr().dtypeSize;

        if ( length <= obj().shortMsgLength() || 
            dynamic_cast<LoopReq*>( ctx->msg() ) ) {

            dbg().verbose(CALL_INFO,2,1,"copyIoVec() short|loop message\n");

            delay = copyIoVec( req->ioVec(), ctx->ioVec(), length );
        }
    }

    obj().schedCallback( 
        std::bind( &ProcessQueuesState<T1>::processShortList2, this, stack ),
        delay
    );
    
}

template< class T1 >
void ProcessQueuesState<T1>::processShortList2(std::deque<FuncCtxBase*>* stack )
{
    dbg().verbose(CALL_INFO,2,1,"stack.size()=%lu\n", stack->size()); 
    ProcessShortListCtx* ctx = static_cast<ProcessShortListCtx*>(stack->back());

    if ( ctx->req ) {

        _CommReq* req = ctx->req;

        size_t length = ctx->hdr().count * ctx->hdr().dtypeSize;

        LoopReq* loopReq;
        if ( ( loopReq = dynamic_cast<LoopReq*>( ctx->msg() ) ) ) {
		    dbg().verbose(CALL_INFO,1,2,"loop message key=%p srcCore=%d srcRank=%d\n", 
                                        loopReq->key, loopReq->srcCore, ctx->hdr().rank);
            req->setDone();
            obj().loopSend( loopReq->srcCore , loopReq->key );

        } else if ( length <= obj().shortMsgLength() ) { 
		    dbg().verbose(CALL_INFO,1,1,"short\n");
            req->setDone(obj().recvReqFiniDelay( length ));
        } else {

            dbg().verbose(CALL_INFO,1,1,"long\n");
            Callback* callback = new Callback;
            *callback = std::bind( &ProcessQueuesState<T1>::getFini,this, req );

            nid_t nid = calcNid( ctx->req, ctx->hdr().rank );

            obj().nic().get( nid, ctx->hdr().key, req->ioVec(), callback );

            req->m_ackKey = ctx->hdr().key; 
            req->m_ackNid = nid;

        }
        ctx->removeMsg();
    } else {
        ctx->incPos();
    }

    if ( ctx->isDone() ) {
        dbg().verbose(CALL_INFO,2,1,"return up the stack\n");

		if ( ! ctx->msgQempty() ) {
			m_recvdMsgQ.insert( m_recvdMsgQ.begin(), ctx->getMsgQ().begin(),
														ctx->getMsgQ().end() );
		}
        delete stack->back(); 
        stack->pop_back();
        obj().schedCallback( stack->back()->getCallback() );
    } else {
        dbg().verbose(CALL_INFO,1,1,"work on next Msg\n");

        processShortList0( stack );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::processLoopResp( LoopResp* resp )
{
    dbg().verbose(CALL_INFO,1,2,"srcCore=%d\n",resp->srcCore );
    _CommReq* req = (_CommReq*)resp->key;  
    req->setDone();
    delete resp;
}

template< class T1 >
void ProcessQueuesState<T1>::getFini( _CommReq* req )
{
    dbg().verbose(CALL_INFO,1,1,"\n");
    m_longGetFiniQ.push_back( req );

    foo();
}

template< class T1 >
void ProcessQueuesState<T1>::dmaRecvFiniGI( GetInfo* info, nid_t nid, 
                                            uint32_t tag, size_t length )
{
    dbg().verbose(CALL_INFO,1,1,"nid=%d tag=%#x length=%lu\n",
                                                    nid, tag, length );
    assert( ( tag & LongAckKey ) == LongAckKey );
    info->nid = nid;
    info->tag = tag;
    info->length = length;
    m_longAckQ.push_back( info );

    foo();
}

template< class T1 >
void ProcessQueuesState<T1>::dmaRecvFiniSRB( ShortRecvBuffer* buf, nid_t nid,
                                            uint32_t tag, size_t length )
{
    dbg().verbose(CALL_INFO,1,1,"ShortMsgQ nid=%#x tag=%#x length=%lu\n",
                                                    nid, tag, length );
    dbg().verbose(CALL_INFO,1,1,"ShortMsgQ rank=%d tag=%#" PRIx64 " count=%d "
                            "dtypeSize=%d\n", buf->hdr.rank, buf->hdr.tag,
                            buf->hdr.count, buf->hdr.dtypeSize );

    assert( tag == (uint32_t) ShortMsgQ );
    m_recvdMsgQ.push_back( buf );

    foo();
    m_postedShortBuffers.erase(buf);
    dbg().verbose(CALL_INFO,1,1,"num postedShortRecvBuffers %lu\n",
                                        m_postedShortBuffers.size());
}

template< class T1 >
void ProcessQueuesState<T1>::enableInt( FuncCtxBase* ctx,
	void (ProcessQueuesState<T1>::*funcPtr)( std::deque< FuncCtxBase* >*) )
{
  	dbg().verbose(CALL_INFO,2,1,"ctx=%p\n",ctx);
	assert( m_funcStack.empty() );
    if ( m_intCtx ) {
  	    dbg().verbose(CALL_INFO,2,1,"already have a return ctx\n");
        return; 
    }

	m_funcStack.push_back(ctx);

	ctx->setCallback( bind( funcPtr, this, &m_funcStack ) );

	m_intCtx = ctx;

	if ( m_missedInt ) foo();
}

template< class T1 >
void ProcessQueuesState<T1>::foo( )
{
	if ( ! m_intCtx || ! m_intStack.empty() ) {
    	dbg().verbose(CALL_INFO,2,1,"missed interrupt\n");
        m_missedInt = true;
		return;
	} 

    InterruptCtx* ctx = new InterruptCtx(
            std::bind( &ProcessQueuesState<T1>::foo0, this, &m_intStack ) 
    ); 

    m_intStack.push_back( ctx );

	processQueues( m_intStack );
}

template< class T1 >
void ProcessQueuesState<T1>::foo0(std::deque<FuncCtxBase*>* stack )
{
    assert( 1 == stack->size() );
	delete stack->back();
	stack->pop_back();

    dbg().verbose(CALL_INFO,2,1,"\n" );

	Callback callback = m_intCtx->getCallback();

	m_intCtx = NULL;

    callback();

    if ( m_intCtx && m_missedInt ) {
        foo();
        m_missedInt = false;
    }
}

template< class T1 >
void ProcessQueuesState<T1>::pioSendFiniVoid( void* hdr )
{
    dbg().verbose(CALL_INFO,1,1,"hdr=%p\n", hdr );
    if ( hdr ) {
        free( hdr );
    }
}

template< class T1 >
void ProcessQueuesState<T1>::pioSendFiniCtrlHdr( CtrlHdr* hdr )
{
    dbg().verbose(CALL_INFO,1,1,"MsgHdr, Ack sent key=%#x\n", hdr->key);
    delete hdr;
    foo();
}

template< class T1 >
void ProcessQueuesState<T1>::processLongGetFini( 
                        std::deque< FuncCtxBase* >& stack, _CommReq* req )
{
    ProcessLongGetFiniCtx* ctx = new ProcessLongGetFiniCtx( req );
    dbg().verbose(CALL_INFO,1,1,"\n");

    stack.push_back( ctx );

    obj().schedCallback( 
        std::bind( &ProcessQueuesState<T1>::processLongGetFini0, this, &stack),
        obj().sendAckDelay() 
    );
}

template< class T1 >
void ProcessQueuesState<T1>::processLongGetFini0( 
                            std::deque< FuncCtxBase* >* stack )
{
    _CommReq* req = static_cast<ProcessLongGetFiniCtx*>(stack->back())->req;

    dbg().verbose(CALL_INFO,1,1,"\n");
    
    delete stack->back();
    stack->pop_back();
    int delay = obj().recvReqFiniDelay( req->getLength() );

    // time to unregister memory
    delay += obj().regRegionDelay( req->getLength() );

    req->setDone( delay );

    IoVec hdrVec;   
    CtrlHdr* hdr = new CtrlHdr;
    hdrVec.ptr = hdr; 
    hdrVec.len = sizeof( *hdr ); 

    Callback* callback = new Callback;
    *callback = std::bind( 
                &ProcessQueuesState<T1>::pioSendFiniCtrlHdr, this, hdr ); 

    std::vector<IoVec> vec;
    vec.insert( vec.begin(), hdrVec );

    dbg().verbose(CALL_INFO,1,1,"send long msg Ack to nid=%d key=%#x\n", 
                                                req->m_ackNid, req->m_ackKey );

    obj().nic().pioSend( req->m_ackNid, req->m_ackKey, vec, callback );

    obj().schedCallback( stack->back()->getCallback() );
}


template< class T1 >
void ProcessQueuesState<T1>::processLongAck( GetInfo* info )
{
    dbg().verbose(CALL_INFO,1,1,"acked\n");
    int delay =  obj().sendReqFiniDelay( info->req->getLength() );
#if 0 
    // time to unregister memory
    if ( info->req->getLength() > obj().shortMsgLength() ) { 
        delay += obj().regRegionDelay( info->req->getLength() );
    }
#endif
    info->req->setDone( delay );
    delete info;
    return;
}

template< class T1 >
void ProcessQueuesState<T1>::needRecv( int nid, int tag, size_t length  )
{
    dbg().verbose(CALL_INFO,1,1,"nid=%d tag=%#x length=%lu\n",nid,tag,length);
    if ( tag != (uint32_t) ShortMsgQ ) {
        dbg().fatal(CALL_INFO,0,"nid=%d tag=%#x length=%lu\n",nid,tag,length);
    }

    ++m_needRecv;
    foo();
}

template< class T1 >
void ProcessQueuesState<T1>::loopHandler( int srcCore, void* key )
{
    dbg().verbose(CALL_INFO,1,2,"resp: srcCore=%d key=%p \n",srcCore,key);

    m_loopResp.push_back( new LoopResp( srcCore, key ) );

    foo();
}

template< class T1 >
void ProcessQueuesState<T1>::loopHandler( int srcCore, std::vector<IoVec>& vec, void* key )
{
        
    MatchHdr* hdr = (MatchHdr*) vec[0].ptr;

    dbg().verbose(CALL_INFO,1,2,"req: srcCore=%d key=%p vec.size()=%lu srcRank=%d\n",
                                                    srcCore, key, vec.size(), hdr->rank);

    ++m_numRecvLooped;
    m_recvdMsgQ.push_back( new LoopReq( srcCore, vec, key ) );

    foo();
}

template< class T1 >
_CommReq* ProcessQueuesState<T1>::searchPostedRecv( MatchHdr& hdr, int& delay )
{
    _CommReq* req = NULL;
    int count = 0;
    dbg().verbose(CALL_INFO,1,1,"posted size %lu\n",m_pstdRcvQ.size());

    std::deque< _CommReq* >:: iterator iter = m_pstdRcvQ.begin();
    for ( ; iter != m_pstdRcvQ.end(); ++iter ) {

        ++count;
        if ( ! checkMatchHdr( hdr, (*iter)->hdr(), (*iter)->ignore() ) ) {
            continue;
        }

        req = *iter;
        m_pstdRcvQ.erase(iter);
        break;
    }
    dbg().verbose(CALL_INFO,2,1,"req=%p\n",req);
    delay = obj().matchDelay(count);

    return req;
}

template< class T1 >
bool ProcessQueuesState<T1>::checkMatchHdr( MatchHdr& hdr, MatchHdr& wantHdr,
                                    uint64_t ignore )
{
    dbg().verbose(CALL_INFO,1,1,"posted tag %#" PRIx64 ", msg tag %#" PRIx64 "\n", wantHdr.tag, hdr.tag );
    if ( ( AnyTag != wantHdr.tag ) && 
            ( ( wantHdr.tag & ~ignore) != ( hdr.tag & ~ignore ) ) ) {
        return false;
    }

    dbg().verbose(CALL_INFO,1,1,"want rank %d %d\n", wantHdr.rank, hdr.rank );
    if ( ( MP::AnySrc != wantHdr.rank ) && ( wantHdr.rank != hdr.rank ) ) {
        return false;
    }

    dbg().verbose(CALL_INFO,1,1,"want group %d %d\n", wantHdr.group,hdr.group);
    if ( wantHdr.group != hdr.group ) {
        return false;
    }

    dbg().verbose(CALL_INFO,1,1,"want count %d %d\n", wantHdr.count,
                                    hdr.count);
    if ( wantHdr.count !=  hdr.count ) {
        return false;
    }

    dbg().verbose(CALL_INFO,1,1,"want dtypeSize %d %d\n", wantHdr.dtypeSize,
                                    hdr.dtypeSize);
    if ( wantHdr.dtypeSize !=  hdr.dtypeSize ) {
        return false;
    }

    dbg().verbose(CALL_INFO,1,1,"matched\n");
    return true;
}

template< class T1 >
int ProcessQueuesState<T1>::copyIoVec( 
                std::vector<IoVec>& dst, std::vector<IoVec>& src, size_t len )
{
    dbg().verbose(CALL_INFO,1,1,"dst.size()=%lu src.size()=%lu wantLen=%lu\n",
                                    dst.size(), src.size(), len );

    size_t copied = 0;
    size_t rV = 0,rP =0;
    for ( unsigned int i=0; i < src.size() && copied < len; i++ ) 
    {
        assert( rV < dst.size() );
        dbg().verbose(CALL_INFO,3,1,"src[%d].len %lu\n", i, src[i].len);

        for ( unsigned int j=0; j < src[i].len && copied < len ; j++ ) {
            dbg().verbose(CALL_INFO,3,1,"copied=%lu rV=%lu rP=%lu\n",
                                                        copied,rV,rP);

            if ( dst[rV].ptr && src[i].ptr ) {
                ((char*)dst[rV].ptr)[rP] = ((char*)src[i].ptr)[j];
            }
            ++copied;
            ++rP;
            if ( rP == dst[rV].len ) {
                rP = 0;
                ++rV;
            } 
        }
    }
    return obj().rxMemcpyDelay( copied );
}

template< class T1 >
void ProcessQueuesState<T1>::print( char* buf, int len )
{
    dbg().verbose(CALL_INFO,3,1,"addr=%p len=%d\n",buf,len);
    std::string tmp;
    for ( int i = 0; i < len; i++ ) {
        dbg().verbose(CALL_INFO,3,1,"%#03x\n",(unsigned char)buf[i]);
    }
}

template< class T1 >
void ProcessQueuesState<T1>::postShortRecvBuffer( )
{
    ShortRecvBuffer* buf =
            new ShortRecvBuffer( obj().shortMsgLength() + sizeof(MatchHdr));

    Callback2* callback = new Callback2;
    *callback = std::bind( 
        &ProcessQueuesState<T1>::dmaRecvFiniSRB, this, buf, 
        std::placeholders::_1,
        std::placeholders::_2, 
        std::placeholders::_3
    ); 

    // save this info so we can cleanup in the destructor 
    m_postedShortBuffers[buf ] = callback;

    dbg().verbose(CALL_INFO,1,1,"num postedShortRecvBuffers %lu\n",
                                        m_postedShortBuffers.size());
    obj().nic().dmaRecv( -1, ShortMsgQ, buf->ioVec, callback ); 
}

}
}
}

#endif

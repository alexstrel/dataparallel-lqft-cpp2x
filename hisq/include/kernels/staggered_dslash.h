#pragma once
#include <algorithm>
#include <execution>
//
#include <core/cartesian_product.hpp>
//
#include <fields/field_accessor.h>

template<typename T>
class DslashParam{
  public:
    const T M;
};

constexpr bool is_constant = true;

template <GaugeFieldViewTp gauge_tp>
class StaggeredDslashArgs {
  public:
    using gauge_data_tp  = typename gauge_tp::data_tp;	  

    static constexpr std::size_t nDir   = gauge_tp::Ndir();
    static constexpr std::size_t nDim   = gauge_tp::Ndim();    
    
//!    static constexpr std::size_t bSize  = bSize_;    

    static constexpr std::size_t bSize  = 1;    
    
    using LinkAccessor = FieldAccessor<gauge_tp, is_constant, bSize>;//only constant links   
    using LinkTp       = LinkAccessor::LinkTp;

    const LinkAccessor U;//gauge field
    const LinkAccessor L;//long links    

    StaggeredDslashArgs( const gauge_tp &u, const gauge_tp &l) : U(u), L(l) {}
};


template <typename Arg>
class StaggeredDslash{
  public:
    using ArgTp  = typename std::remove_cvref_t<Arg>;

    const Arg &args;

    StaggeredDslash(const Arg &args) : args(args) {}  
    
    /** Convert tuple into std::array in the inverse order:
    */
    template<std::size_t... Id>
    inline decltype(auto) get_cartesian_coords(std::index_sequence<Id...>, const auto& x) const {
      return std::array<int, sizeof... (Id)>{{std::get<sizeof...(Id)-Id-1>(x)... }};
    }

    inline decltype(auto) convert_coords(const auto &x) const {
      return get_cartesian_coords(std::make_index_sequence<ArgTp::nDim>{}, x);
    }       

    inline decltype(auto) compute_parity_site_stencil(const auto &in, const FieldParity parity, auto &X){
    
      using Link   = ArgTp::LinkTp; 
      using Spinor = typename std::remove_cvref_t<decltype(in)>::SpinorTp;
      
      const int mask = (X[1]*X[2]*X[3]) & 1;
	      	      
      const int parity_bit = parity == FieldParity::EvenFieldParity ? mask : 1 - mask;
      //
      const int my_parity    = parity == FieldParity::EvenFieldParity ? 0 : 1;
      const int other_parity = 1 - my_parity;
      
      Spinor res; 
      //      	 	      
#pragma unroll
      for (int d = 0; d < ArgTp::nDir; d++) {
      
        const int Xd = X[d];
                
	// Standard fwd gather:
	{  
          //standard forward direction          
	  if ( (Xd == (in.Extent(d) - 1)) and (d != 0 or (d == 0 and parity_bit == 1)) ) {
	    //	
            const Link U_  = args.U(X,d, my_parity);

	    X[d] = 0;

            const Spinor in_ = in(X);
	    //
            res += U_*in_;		                  
	  } else {
            const Link U_ = args.U(X,d, my_parity);

	    X[d] = X[d] + (d == 0 ? parity_bit : 1);

            const Spinor in_ = in(X);
	    //		  
            res += U_*in_;		  
	  }	  
          //
          X[d] = Xd;	  
	}
	//  Improved fwd gather:
	{ 
	  const int Xf = d == 0 ? 2*Xd + parity_bit : Xd;

	  if ( Xf >= (in.Extent(d) - 3) ) {
	    //	
            const Link U_  = args.U(X,d, my_parity);

	    X[d] = d == 0 ? (Xf - 2*in.Extent(d) + 3) / 2 : (Xf - in.Extent(d) + 3);

            const Spinor in_ = in(X);
	    //
            res += U_*in_;		                  
	  } else {
            const Link U_ = args.U(X,d, my_parity);

	    X[d] = X[d] + (d == 0 ? (3+parity_bit) / 2 : 3);

            const Spinor in_ = in(X);
	    //		  
            res += U_*in_;		  
	  }	  
          //
          X[d] = Xd;	  
	}
	// Bwd neighbour contribution:
	{
          if ( (Xd == 0) and (d != 0 or (d == 0 and parity_bit == 0)) ) {
            //  
	    X[d] = (in.Extent(d)-1);	  

	    const Link U_    = args.U(X, d, other_parity);
	    const Spinor in_ = in(X);
            //
            res += conj(U_)*in_;              	    
          } else {  		
	    
	    X[d] = X[d] - (d == 0 ? (1- parity_bit) : 1);

	    const Link U_    = args.U(X,d, other_parity);
	    const Spinor in_ = in(X);
            //
	    res += conj(U_)*in_;	 
	  }
          //
          X[d] = Xd;	            
	}
	// Bwd neighbour contribution:
	{
  	  const int Xf = d == 0 ? 2*Xd + parity_bit : Xd;
	
          if ( Xf < 3 ) {
            //  
	    X[d] = d == 0 ? (Xf + 2*in.Extent(d) - 3) / 2 : Xf + in.Extent(d) - 3;	  

	    const Link U_    = args.U(X, d, other_parity);
	    const Spinor in_ = in(X);
            //
            res += conj(U_)*in_;              	    
          } else {  		
	    
	    X[d] = X[d] - (d == 0 ? (3 + (1 - parity_bit)) / 2 : 3);//?

	    const Link U_    = args.U(X,d, other_parity);
	    const Spinor in_ = in(X);
            //
	    res += conj(U_)*in_;	 
	  }
          //
          X[d] = Xd;	            
	}	
      }

      return res;
    }     
 
    void apply(GenericStaggeredSpinorFieldViewTp auto &out_spinor,
               const GenericStaggeredSpinorFieldViewTp auto &in_spinor,
               const GenericStaggeredSpinorFieldViewTp auto &aux_spinor,
               auto &&post_transformer,               
               const auto cartesian_idx,
               const FieldParity parity) {	    
      // Dslash_nm = (M + 2r) \delta_nm - 0.5 * \sum_\mu  ((r - \gamma_\mu)*U_(x){\mu}*\delta_{m,n+\mu} + (r + \gamma_\mu)U^*(x-mu)_{\mu}\delta_{m,n-\mu})
      //
      // gamma_{1/2} -> sigma_{1/2}, gamma_{5} -> sigma_{3}
      //
      using S = typename std::remove_cvref_t<decltype(out_spinor[0])>;       

      auto X  = convert_coords(cartesian_idx);
      //
      auto X_view = X | std::views::all;
#pragma unroll
      for ( int i = 0; i < out_spinor.size(); i++ ){  	      
      
        auto out         = FieldAccessor<S>{out_spinor[i]};
        const auto in    = FieldAccessor<S, is_constant>{in_spinor[i]};
        const auto aux   = FieldAccessor<S, is_constant>{aux_spinor[i]};        
        //
        auto res = compute_parity_site_stencil(in, parity, X_view);
        //
        const auto aux_  = aux(X_view);
        //
        post_transformer(aux_, res);
        //
#pragma unroll
        for (int c = 0; c < S::Ncolor(); c++){
          out(X_view,c) = res(c);//FIXME : works only for bSize = 1
        }        
      }//end of for loop
    }    

    void apply(GenericStaggeredSpinorFieldViewTp auto &out_spinor,
               const GenericStaggeredSpinorFieldViewTp auto &in_spinor,
               const auto cartesian_idx,
               const FieldParity parity) {	    
      // Dslash_nm = \sum_\mu  ((r - \gamma_\mu)*U_(x){\mu}*\delta_{m,n+\mu} + (r + \gamma_\mu)U^*(x-mu)_{\mu}\delta_{m,n-\mu})
      //
      // gamma_{1/2} -> sigma_{1/2}, gamma_{5} -> sigma_{3}
      //
      using S = typename std::remove_cvref_t<decltype(out_spinor[0])>; 

      auto X  = convert_coords(cartesian_idx);
      //
      auto X_view = X | std::views::all;
#pragma unroll
      for ( int i = 0; i < out_spinor.size(); i++ ){  	      
      
        auto out         = FieldAccessor<S>{out_spinor[i]};
        const auto in    = FieldAccessor<S, is_constant>{in_spinor[i]};
        //
        auto res = compute_parity_site_stencil(in, parity, X_view);
    
#pragma unroll
        for (int c = 0; c < S::Ncolor(); c++){
          out(X_view,c) = res(c);//FIXME : works only for bSize = 1
        }
      }//end of for loop
    }       
};




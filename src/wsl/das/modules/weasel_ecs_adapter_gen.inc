// Auto-generated-style class adapter for EcsSystem.
//
// Bridges C++ virtual method calls to daScript class overrides.
// Based on the pattern from daScript tutorial 19 (class_adapters_gen.inc).
//
// Each method:
//   get_<method>(self)       — returns Func pointer if overridden
//   invoke_<method>(ctx,fn,self,args...) — calls via das_invoke_function

#pragma once

#include "daScript/daScript.h"

using das::adapt_field_offset;
using das::Context;
using das::das_invoke_function;
using das::Func;
using das::getDasClassMethod;
using das::StructInfo;

class EcsSystemAdapter {
protected:
  enum {
    __fn_on_init = 0,
    __fn_on_update = 1,
    __fn_on_inactive = 2,
    __fn_on_fixed_update = 3,
    __fn_on_late_update = 4,
  };

 protected:
  static inline int _das_class_method_offset[5];

 public:
  EcsSystemAdapter ( const StructInfo * info ) {
    if ( _das_class_method_offset[__fn_on_init] == 0 ) {
      _das_class_method_offset[__fn_on_init]
        = adapt_field_offset("on_init", info);
      _das_class_method_offset[__fn_on_update]
        = adapt_field_offset("on_update", info);
      _das_class_method_offset[__fn_on_inactive]
        = adapt_field_offset("on_inactive", info);
      _das_class_method_offset[__fn_on_fixed_update]
        = adapt_field_offset("on_fixed_update", info);
      _das_class_method_offset[__fn_on_late_update]
        = adapt_field_offset("on_late_update", info);
    }
  }

  // ── on_init ──

  static __forceinline Func get_on_init ( void * self ) {
    return getDasClassMethod(self, _das_class_method_offset[__fn_on_init]);
  }

  static __forceinline void invoke_on_init ( Context * __context__,
      Func __funcCall__, void * self ) {
    das_invoke_function<void>::invoke<void *>(
        __context__, nullptr, __funcCall__, self);
  }

  // ── on_update(dt : float) ──

  static __forceinline Func get_on_update ( void * self ) {
    return getDasClassMethod(self, _das_class_method_offset[__fn_on_update]);
  }

  static __forceinline void invoke_on_update ( Context * __context__,
      Func __funcCall__, void * self, float dt ) {
    das_invoke_function<void>::invoke<void *, float>(
        __context__, nullptr, __funcCall__, self, dt);
  }

  // ── on_inactive() ──

  static __forceinline Func get_on_inactive ( void * self ) {
    return getDasClassMethod(self, _das_class_method_offset[__fn_on_inactive]);
  }

  static __forceinline void invoke_on_inactive ( Context * __context__,
      Func __funcCall__, void * self ) {
    das_invoke_function<void>::invoke<void *>(
        __context__, nullptr, __funcCall__, self);
  }

  // ── on_fixed_update(fixed_dt : float) ──

  static __forceinline Func get_on_fixed_update ( void * self ) {
    return getDasClassMethod(self,
        _das_class_method_offset[__fn_on_fixed_update]);
  }

  static __forceinline void invoke_on_fixed_update ( Context * __context__,
      Func __funcCall__, void * self, float fixed_dt ) {
    das_invoke_function<void>::invoke<void *, float>(
        __context__, nullptr, __funcCall__, self, fixed_dt);
  }

  // ── on_late_update(dt : float) ──

  static __forceinline Func get_on_late_update ( void * self ) {
    return getDasClassMethod(self,
        _das_class_method_offset[__fn_on_late_update]);
  }

  static __forceinline void invoke_on_late_update ( Context * __context__,
      Func __funcCall__, void * self, float dt ) {
    das_invoke_function<void>::invoke<void *, float>(
        __context__, nullptr, __funcCall__, self, dt);
  }
};

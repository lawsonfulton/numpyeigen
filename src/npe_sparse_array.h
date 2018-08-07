#ifndef NPE_SPARSE_ARRAY_H
#define NPE_SPARSE_ARRAY_H
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <iostream>
#include <tuple>
#include <pybind11/eigen.h>
#include <numpy/arrayobject.h>

#include <Eigen/Core>

namespace npe {

// TODO: Cache attributes so we don't do expensive lookups more than once
struct sparse_array : pybind11::object {
  PYBIND11_OBJECT(sparse_array, pybind11::object, PyType_Check)

  sparse_array() = default;

  pybind11::dtype dtype() const {
    auto atr = this->attr("dtype");
    return pybind11::reinterpret_borrow<pybind11::dtype>(atr);
  }

  pybind11::array data() const {
    auto atr = this->attr("data");
    return pybind11::reinterpret_borrow<pybind11::array>(atr);
  }

  pybind11::array indices() const {
    auto atr = this->attr("indices");
    return pybind11::reinterpret_borrow<pybind11::array>(atr);
  }

  pybind11::array indptr() const {
    auto atr = this->attr("indptr");
    return pybind11::reinterpret_borrow<pybind11::array>(atr);
  }

  std::array<ssize_t, 2> shape() const {
    auto sz = this->attr("shape").cast<std::pair<ssize_t, ssize_t>>();
    return {{sz.first, sz.second}};
  }

  constexpr ssize_t ndim() const {
    return 2;
  }

  std::string getformat() const {
    return this->attr("getformat")().cast<std::string>();
  }

  bool row_major() const {
    return getformat() == "csr";
  }

  bool col_major() const {
    return !row_major();
  }

  int flags() const {
    return _flags;
  }

  int nnz() const {
    return this->attr("nnz").cast<int>();
  }

  template <typename T>
  Eigen::MappedSparseMatrix<typename T::Scalar, T::Options, typename T::StorageIndex> as_eigen() {
    auto shape = this->shape();
    pybind11::array data = this->data();
    pybind11::array indices = this->indices();
    pybind11::array indptr = this->indptr();

    // TODO: Use Eigen::Map in post 3.3
    typedef Eigen::MappedSparseMatrix<typename T::Scalar, T::Options, typename T::StorageIndex> RetMap;
    return RetMap(shape[0], shape[1], this->nnz(),
                  (typename T::StorageIndex*) indptr.data(),
                  (typename T::StorageIndex*) indices.data(),
                  (typename T::Scalar*) data.data());
  }

private:
  friend class pybind11::detail::type_caster<npe::sparse_array>;
  int _flags = 0;
};

}

namespace pybind11 {
namespace detail {


template <>
struct type_caster<npe::sparse_array> {
public:
  PYBIND11_TYPE_CASTER(npe::sparse_array, _("scipy.sparse.csr_matrix | scipy.sparse.csc_matrix"));

  /**
   * Conversion part 1 (Python->C++): convert a PyObject into a npe::sparse_array
   * instance or return false upon failure. The second argument
   * indicates whether implicit conversions should be applied.
   */
  bool load(handle src, bool) {
    if (!src) {
      return false;
    }

    // TODO: attribute lookups are kind of slow and I would like to avoid them so lets cache the results
    // TODO: Check if attribute lookup succeeded
    try {
      std::string fmt = pybind11::getattr(src, "getformat")().cast<std::string>();
      pybind11::getattr(src, "data").cast<pybind11::array>();
      pybind11::getattr(src, "indices").cast<pybind11::array>();
      pybind11::getattr(src, "indptr").cast<pybind11::array>();
      if (fmt != "csr" && fmt != "csc") {
        return false;
      }

      value = pybind11::reinterpret_borrow<npe::sparse_array>(src);
      value._flags |= (value.col_major() ? NPY_ARRAY_F_CONTIGUOUS : NPY_ARRAY_C_CONTIGUOUS);

    } catch (pybind11::cast_error) {
      return false;
    }

    return true;
  }

  /**
   * Conversion part 2 (C++ -> Python): convert a npe::sparse_array instance into
   * a Python object. The second and third arguments are used to
   * indicate the return value policy and parent object (for
   * ``return_value_policy::reference_internal``) and are generally
   * ignored by implicit casters.
   */
  static handle cast(npe::sparse_array src, return_value_policy /* policy */, handle /* parent */) {
    return src.release();
  }

};


// Casts an Eigen Sparse type to scipy csr_matrix or csc_matrix.
// If given a base, the numpy array references the src data, otherwise it'll make a copy.
// writeable lets you turn off the writeable flag for the array.
template <typename Type, typename = enable_if_t<is_eigen_sparse<Type>::value>>
handle eigen_sparse_array_cast(Type* src, handle parent = none(), bool writable = true) {
  bool rowMajor = Type::Flags | Eigen::RowMajor;

  array data = array(src->nonZeros(), src->valuePtr(), parent);
  data.flags() &= ~detail::npy_api::NPY_ARRAY_OWNDATA_;

  array indptr = array((rowMajor ? src->rows() : src->cols()) + 1, src->outerIndexPtr(), parent);
  indptr.flags() &= ~detail::npy_api::NPY_ARRAY_OWNDATA_;

  array indices = array(src->nonZeros(), src->innerIndexPtr(), parent);
  indices.flags() &= ~detail::npy_api::NPY_ARRAY_OWNDATA_;

  object sparse_module = module::import("scipy.sparse");
  object matrix_type = sparse_module.attr(
      rowMajor ? "csr_matrix" : "csc_matrix");

  if (!writable) {
    indices.flags() &= ~detail::npy_api::NPY_ARRAY_WRITEABLE_;
    indptr.flags() &= ~detail::npy_api::NPY_ARRAY_WRITEABLE_;
    data.flags() &= ~detail::npy_api::NPY_ARRAY_WRITEABLE_;
  }
  return matrix_type(std::make_tuple(data, indices, indptr),
                     std::make_pair(src->rows(), src->cols()), "copy"_a=false).release();
}

template <typename Type, typename = enable_if_t<is_eigen_sparse<Type>::value>>
handle eigen_encapsulate_sparse(Type* src) {
  capsule capsule_base(src, [](void *o) {
    delete static_cast<Type *>(o);
  });
  return eigen_sparse_array_cast(src, capsule_base);
}

//template<typename Type>
//struct type_caster<Type, enable_if_t<is_eigen_sparse<Type>::value>> {
//  using Scalar = typename Type::Scalar;
//  using props = EigenProps<Type>;

//  Eigen::MappedSparseMatrix<typename Type::Scalar, Type::Options, typename Type::StorageIndex> map_value;

//private:
//  // Cast implementation
//  template <typename CType>
//  static handle cast_impl(CType *src, return_value_policy policy, handle parent) {
//      switch (policy) {
//          case return_value_policy::take_ownership:
//          case return_value_policy::automatic:
//              return eigen_encapsulate_sparse(src);
//          case return_value_policy::move:
//              std::cerr << "Encapsulate move!" << std::endl;
//              return eigen_encapsulate_sparse(new CType(std::move(*src)));
//          case return_value_policy::copy:
//              return eigen_sparse_array_cast(src);
//          case return_value_policy::reference:
//          case return_value_policy::automatic_reference:
//              return eigen_sparse_array_cast(src, none() /* parent */, !std::is_const<CType>::value);
//          case return_value_policy::reference_internal:
//              return eigen_sparse_array_cast(src, none() /* parent */, !std::is_const<CType>::value);
//          default:
//              throw cast_error("unhandled return_value_policy: should not happen!");
//      };
//  }

//public:

//  bool load(handle src, bool convert) {
//    // TODO Maybe always return false
//    if (!src) {
//      return false;
//    }

//    npe::sparse_array arr = src.cast<npe::sparse_array>();
//    if (!arr()) {
//      return false;
//    }
//    map_value = arr.as_eigen<Type>();
//    return true;
//  }

//  // Normal returned non-reference, non-const value:
//  static handle cast(Type &&src, return_value_policy /* policy */, handle parent) {
//      return cast_impl(&src, return_value_policy::move, parent);
//  }
//  // If you return a non-reference const, we mark the numpy array readonly:
//  static handle cast(const Type &&src, return_value_policy /* policy */, handle parent) {
//      return cast_impl(&src, return_value_policy::move, parent);
//  }
//  // lvalue reference return; default (automatic) becomes copy
//  static handle cast(Type &src, return_value_policy policy, handle parent) {
//      if (policy == return_value_policy::automatic || policy == return_value_policy::automatic_reference)
//          policy = return_value_policy::copy;
//      return cast_impl(&src, policy, parent);
//  }
//  // const lvalue reference return; default (automatic) becomes copy
//  static handle cast(const Type &src, return_value_policy policy, handle parent) {
//      if (policy == return_value_policy::automatic || policy == return_value_policy::automatic_reference)
//          policy = return_value_policy::copy;
//      return cast(&src, policy, parent);
//  }
//  // non-const pointer return
//  static handle cast(Type *src, return_value_policy policy, handle parent) {
//      return cast_impl(src, policy, parent);
//  }
//  // const pointer return
//  static handle cast(const Type *src, return_value_policy policy, handle parent) {
//      return cast_impl(src, policy, parent);
//  }

//  static PYBIND11_DESCR name() { return props::descriptor(); }

//  operator Type*() { return &value; }
//  operator Type&() { return value; }
//  operator Type&&() && { return std::move(value); }
//  template <typename T> using cast_op_type = movable_cast_op_type<T>;

//private:
//  Type value;
//};

}
}

#endif // NPE_SPARSE_ARRAY_H
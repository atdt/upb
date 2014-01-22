//
// upb - a minimalist implementation of protocol buffers.
//
// Copyright (c) 2011-2012 Google Inc.  See LICENSE for details.
// Author: Josh Haberman <jhaberman@gmail.com>
//
// Note that we have received an exception from c-style-artiters regarding
// dynamic_cast<> in this file:
// https://groups.google.com/a/google.com/d/msg/c-style/7Zp_XCX0e7s/I6dpzno4l-MJ
//
// IMPORTANT NOTE!  This file is compiled TWICE, once with UPB_GOOGLE3 defined
// and once without!  This allows us to provide functionality against proto2
// and protobuf opensource both in a single binary without the two conflicting.
// However we must be careful not to violate the ODR.

#include "upb/bindings/googlepb/proto2.h"

#include "upb/def.h"
#include "upb/bindings/googlepb/proto1.h"
#include "upb/handlers.h"
#include "upb/shim/shim.h"
#include "upb/sink.h"

namespace {

template<typename To, typename From> To CheckDownCast(From* f) {
  assert(f == NULL || dynamic_cast<To>(f) != NULL);
  return static_cast<To>(f);
}

}

// Unconditionally evaluate, but also assert in debug mode.
#define CHKRET(x) do { bool ok = (x); UPB_UNUSED(ok); assert(ok); } while (0)

namespace upb {
namespace google_google3 { class GMR_Handlers; }
namespace google_opensource { class GMR_Handlers; }
}  // namespace upb

// BEGIN DOUBLE COMPILATION TRICKERY. //////////////////////////////////////////

#ifdef UPB_GOOGLE3

#include "net/proto2/proto/descriptor.pb.h"
#include "net/proto2/public/descriptor.h"
#include "net/proto2/public/extension_set.h"
#include "net/proto2/public/generated_message_reflection.h"
#include "net/proto2/public/lazy_field.h"
#include "net/proto2/public/message.h"
#include "net/proto2/public/repeated_field.h"
#include "net/proto2/public/string_piece_field_support.h"

namespace goog = ::proto2;
namespace me = ::upb::google_google3;

#else

// TODO(haberman): remove these once new versions of protobuf that "friend"
// upb are pervasive in the wild.
#define protected public
#include "google/protobuf/repeated_field.h"
#undef protected

#define private public
#include "google/protobuf/generated_message_reflection.h"
#undef private

#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/extension_set.h"
#include "google/protobuf/message.h"

namespace goog = ::google::protobuf;
namespace me = ::upb::google_opensource;

using goog::int32;
using goog::int64;
using goog::uint32;
using goog::uint64;
using goog::scoped_ptr;

#endif  // ifdef UPB_GOOGLE3

// END DOUBLE COMPILATION TRICKERY. ////////////////////////////////////////////

// Have to define this manually since older versions of proto2 didn't define
// an enum value for STRING.
#define UPB_CTYPE_STRING 0

template <class T> static T* GetPointer(void* message, size_t offset) {
  return reinterpret_cast<T*>(static_cast<char*>(message) + offset);
}
template <class T>
static const T* GetConstPointer(const void* message, size_t offset) {
  return reinterpret_cast<const T*>(static_cast<const char*>(message) + offset);
}

// This class contains handlers that can write into a proto2 class whose
// reflection class is GeneratedMessageReflection.  (Despite the name, even
// DynamicMessage uses GeneratedMessageReflection, so this covers all proto2
// messages generated by the compiler.)  To do this it must break the
// encapsulation of GeneratedMessageReflection and therefore depends on
// internal interfaces that are not guaranteed to be stable.  This class will
// need to be updated if any non-backward-compatible changes are made to
// GeneratedMessageReflection.
class me::GMR_Handlers {
 public:
  // Returns true if we were able to set an accessor and any other properties
  // of the FieldDef that are necessary to read/write this field to a
  // proto2::Message.
  static bool TrySet(const goog::FieldDescriptor* proto2_f,
                     const goog::Message& m, const upb::FieldDef* upb_f,
                     upb::Handlers* h) {
    const goog::Reflection* base_r = m.GetReflection();
    // See file comment re: dynamic_cast.
    const goog::internal::GeneratedMessageReflection* r =
        dynamic_cast<const goog::internal::GeneratedMessageReflection*>(base_r);
    if (!r) return false;

#define PRIMITIVE_TYPE(cpptype, cident)                                        \
case goog::FieldDescriptor::cpptype:                                           \
  SetPrimitiveHandlers<cident>(proto2_f, r, upb_f, h);                         \
  return true;

    switch (proto2_f->cpp_type()) {
      PRIMITIVE_TYPE(CPPTYPE_INT32, int32);
      PRIMITIVE_TYPE(CPPTYPE_INT64, int64);
      PRIMITIVE_TYPE(CPPTYPE_UINT32, uint32);
      PRIMITIVE_TYPE(CPPTYPE_UINT64, uint64);
      PRIMITIVE_TYPE(CPPTYPE_DOUBLE, double);
      PRIMITIVE_TYPE(CPPTYPE_FLOAT, float);
      PRIMITIVE_TYPE(CPPTYPE_BOOL, bool);
      case goog::FieldDescriptor::CPPTYPE_ENUM:
        if (proto2_f->is_extension()) {
          SetEnumExtensionHandlers(proto2_f, r, upb_f, h);
        } else {
          SetEnumHandlers(proto2_f, r, upb_f, h);
        }
        return true;
      case goog::FieldDescriptor::CPPTYPE_STRING: {
        if (proto2_f->is_extension()) {
#ifdef UPB_GOOGLE3
          SetStringExtensionHandlers<string>(proto2_f, r, upb_f, h);
#else
          SetStringExtensionHandlers<std::string>(proto2_f, r, upb_f, h);
#endif
          return true;
        }

        // Old versions of the open-source protobuf release erroneously default
        // to Cord even though that has never been supported in the open-source
        // release.
        int32_t ctype = proto2_f->options().has_ctype() ?
                        proto2_f->options().ctype()
                        : UPB_CTYPE_STRING;
        switch (ctype) {
#ifdef UPB_GOOGLE3
          case goog::FieldOptions::STRING:
            SetStringHandlers<string>(proto2_f, r, upb_f, h);
            return true;
          case goog::FieldOptions::CORD:
            SetCordHandlers(proto2_f, r, upb_f, h);
            return true;
          case goog::FieldOptions::STRING_PIECE:
            SetStringPieceHandlers(proto2_f, r, upb_f, h);
            return true;
#else
          case UPB_CTYPE_STRING:
            SetStringHandlers<std::string>(proto2_f, r, upb_f, h);
            return true;
#endif
          default:
            return false;
        }
      }
      case goog::FieldDescriptor::CPPTYPE_MESSAGE:
#ifdef UPB_GOOGLE3
        if (proto2_f->options().lazy()) {
          assert(false);
          return false;  // Not yet implemented.
        }
#endif
        if (proto2_f->is_extension()) {
          SetSubMessageExtensionHandlers(proto2_f, m, r, upb_f, h);
          return true;
        }
        SetSubMessageHandlers(proto2_f, m, r, upb_f, h);
        return true;
      default:
        return false;
    }
  }

#undef PRIMITIVE_TYPE

  static const goog::Message* GetFieldPrototype(
      const goog::Message& m, const goog::FieldDescriptor* f) {
    // We assume that all submessages (and extensions) will be constructed
    // using the same MessageFactory as this message.  This doesn't cover the
    // case of CodedInputStream::SetExtensionRegistry().
    // See file comment re: dynamic_cast.
    const goog::internal::GeneratedMessageReflection* r =
        dynamic_cast<const goog::internal::GeneratedMessageReflection*>(
            m.GetReflection());
    if (!r) return NULL;
    return r->message_factory_->GetPrototype(f->message_type());
  }

 private:
  static upb_selector_t GetSelector(const upb::FieldDef* f,
                                    upb::Handlers::Type type) {
    upb::Handlers::Selector selector;
    bool ok = upb::Handlers::GetSelector(f, type, &selector);
    UPB_ASSERT_VAR(ok, ok);
    return selector;
  }

  static int64_t GetHasbit(
      const goog::FieldDescriptor* f,
      const goog::internal::GeneratedMessageReflection* r) {
    // proto2 does not store hasbits for repeated fields.
    assert(!f->is_repeated());
    return (r->has_bits_offset_ * 8) + f->index();
  }

  static uint16_t GetOffset(
      const goog::FieldDescriptor* f,
      const goog::internal::GeneratedMessageReflection* r) {
    return r->offsets_[f->index()];
  }

  class FieldOffset {
   public:
    FieldOffset(const goog::FieldDescriptor* f,
                const goog::internal::GeneratedMessageReflection* r)
        : offset_(GetOffset(f, r)), is_repeated_(f->is_repeated()) {
      if (!is_repeated_) {
        int64_t hasbit = GetHasbit(f, r);
        hasbyte_ = hasbit / 8;
        mask_ = 1 << (hasbit % 8);
      }
    }

    template <class T> T* GetFieldPointer(goog::Message* message) const {
      return GetPointer<T>(message, offset_);
    }

    void SetHasbit(void* m) const {
      assert(!is_repeated_);
      uint8_t* byte = GetPointer<uint8_t>(m, hasbyte_);
      *byte |= mask_;
    }

   private:
    const size_t offset_;
    bool is_repeated_;

    // Only for non-repeated fields.
    int32_t hasbyte_;
    int8_t mask_;
  };

  class ExtensionFieldData {
   public:
    ExtensionFieldData(
        const goog::FieldDescriptor* proto2_f,
        const goog::internal::GeneratedMessageReflection* r)
        : offset_(r->extensions_offset_),
          number_(proto2_f->number()),
          type_(proto2_f->type()) {
    }

    int number() const { return number_; }
    goog::internal::FieldType type() const { return type_; }

    goog::internal::ExtensionSet* GetExtensionSet(goog::Message* m) const {
      return GetPointer<goog::internal::ExtensionSet>(m, offset_);
    }

   private:
    const size_t offset_;
    int number_;
    goog::internal::FieldType type_;
  };

  // StartSequence /////////////////////////////////////////////////////////////

  template <class T>
  static void SetStartRepeatedField(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    CHKRET(h->SetStartSequenceHandler(
        f, UpbBindT(&PushOffset<goog::RepeatedField<T> >,
                    new FieldOffset(proto2_f, r))));
  }

  template <class T>
  static void SetStartRepeatedPtrField(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    CHKRET(h->SetStartSequenceHandler(
        f, UpbBindT(&PushOffset<goog::RepeatedPtrField<T> >,
                    new FieldOffset(proto2_f, r))));
  }

  static void SetStartRepeatedSubmessageField(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    CHKRET(h->SetStartSequenceHandler(
        f, UpbBind(&PushOffset<goog::internal::RepeatedPtrFieldBase>,
                   new FieldOffset(proto2_f, r))));
  }

  template <class T>
  static T* PushOffset(goog::Message* message, const FieldOffset* offset) {
    return offset->GetFieldPointer<T>(message);
  }

  // Primitive Value (numeric, bool) ///////////////////////////////////////////

  template <typename T> static void SetPrimitiveHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    if (proto2_f->is_extension()) {
      scoped_ptr<ExtensionFieldData> data(new ExtensionFieldData(proto2_f, r));
      if (f->IsSequence()) {
        CHKRET(h->SetValueHandler<T>(
            f, UpbBindT(AppendPrimitiveExtension<T>, data.release())));
      } else {
        CHKRET(h->SetValueHandler<T>(
            f, UpbBindT(SetPrimitiveExtension<T>, data.release())));
      }
    } else {
      if (f->IsSequence()) {
        SetStartRepeatedField<T>(proto2_f, r, f, h);
        CHKRET(h->SetValueHandler<T>(f, UpbMakeHandlerT(AppendPrimitive<T>)));
      } else {
        CHKRET(upb::Shim::Set(h, f, GetOffset(proto2_f, r),
                              GetHasbit(proto2_f, r)));
      }
    }
  }

  template <typename T>
  static void AppendPrimitive(goog::RepeatedField<T>* r, T val) { r->Add(val); }

  template <typename T>
  static void AppendPrimitiveExtension(goog::Message* m,
                                       const ExtensionFieldData* data, T val) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    // TODO(haberman): give an accurate value for "packed"
    goog::internal::RepeatedPrimitiveTypeTraits<T>::Add(
        data->number(), data->type(), true, val, set);
  }

  template <typename T>
  static void SetPrimitiveExtension(goog::Message* m,
                                    const ExtensionFieldData* data, T val) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    goog::internal::PrimitiveTypeTraits<T>::Set(data->number(), data->type(),
                                                val, set);
  }

  // Enum //////////////////////////////////////////////////////////////////////

  class EnumHandlerData : public FieldOffset {
   public:
    EnumHandlerData(const goog::FieldDescriptor* proto2_f,
                    const goog::internal::GeneratedMessageReflection* r,
                    const upb::FieldDef* f)
        : FieldOffset(proto2_f, r),
          field_number_(f->number()),
          unknown_fields_offset_(r->unknown_fields_offset_),
          enum_(upb_downcast_enumdef(f->subdef())) {}

    bool IsValidValue(int32_t val) const {
      return enum_->FindValueByNumber(val) != NULL;
    }

    int32_t field_number() const { return field_number_; }

    goog::UnknownFieldSet* mutable_unknown_fields(goog::Message* m) const {
      return GetPointer<goog::UnknownFieldSet>(m, unknown_fields_offset_);
    }

   private:
    int32_t field_number_;
    size_t unknown_fields_offset_;
    const upb::EnumDef* enum_;
  };

  static void SetEnumHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    assert(!proto2_f->is_extension());
    scoped_ptr<EnumHandlerData> data(new EnumHandlerData(proto2_f, r, f));
    if (f->IsSequence()) {
      CHKRET(h->SetInt32Handler(f, UpbBind(AppendEnum, data.release())));
    } else {
      CHKRET(h->SetInt32Handler(f, UpbBind(SetEnum, data.release())));
    }
  }

  static void SetEnum(goog::Message* m, const EnumHandlerData* data,
                      int32_t val) {
    if (data->IsValidValue(val)) {
      int32_t* message_val = data->GetFieldPointer<int32_t>(m);
      *message_val = val;
      data->SetHasbit(m);
    } else {
      data->mutable_unknown_fields(m)->AddVarint(data->field_number(), val);
    }
  }

  static void AppendEnum(goog::Message* m, const EnumHandlerData* data,
                         int32_t val) {
    // Closure is the enclosing message.  We can't use the RepeatedField<> as
    // the closure because we need to go back to the message for unrecognized
    // enum values, which go into the unknown field set.
    if (data->IsValidValue(val)) {
      goog::RepeatedField<int32_t>* r =
          data->GetFieldPointer<goog::RepeatedField<int32_t> >(m);
      r->Add(val);
    } else {
      data->mutable_unknown_fields(m)->AddVarint(data->field_number(), val);
    }
  }

  // EnumExtension /////////////////////////////////////////////////////////////

  static void SetEnumExtensionHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    assert(proto2_f->is_extension());
    scoped_ptr<ExtensionFieldData> data(new ExtensionFieldData(proto2_f, r));
    if (f->IsSequence()) {
      CHKRET(
          h->SetInt32Handler(f, UpbBind(AppendEnumExtension, data.release())));
    } else {
      CHKRET(h->SetInt32Handler(f, UpbBind(SetEnumExtension, data.release())));
    }
  }

  static void SetEnumExtension(goog::Message* m, const ExtensionFieldData* data,
                               int32_t val) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    set->SetEnum(data->number(), data->type(), val, NULL);
  }

  static void AppendEnumExtension(goog::Message* m,
                                  const ExtensionFieldData* data, int32_t val) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    // TODO(haberman): give an accurate value for "packed"
    set->AddEnum(data->number(), data->type(), true, val, NULL);
  }

  // String ////////////////////////////////////////////////////////////////////

  // For scalar (non-repeated) string fields.
  template <class T> class StringHandlerData : public FieldOffset {
   public:
    StringHandlerData(const goog::FieldDescriptor* proto2_f,
                      const goog::internal::GeneratedMessageReflection* r)
        : FieldOffset(proto2_f, r),
          prototype_(*GetConstPointer<T*>(r->default_instance_,
                                          GetOffset(proto2_f, r))) {}

    const T* prototype() const { return prototype_; }

    T** GetStringPointer(goog::Message* message) const {
      return GetFieldPointer<T*>(message);
    }

   private:
    const T* prototype_;
  };

  template <typename T> static void SetStringHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f,
      upb::Handlers* h) {
    assert(!proto2_f->is_extension());
    CHKRET(h->SetStringHandler(f, UpbMakeHandlerT(&OnStringBuf<T>)));
    if (f->IsSequence()) {
      SetStartRepeatedPtrField<T>(proto2_f, r, f, h);
      CHKRET(
          h->SetStartStringHandler(f, UpbMakeHandlerT(StartRepeatedString<T>)));
    } else {
      CHKRET(h->SetStartStringHandler(
          f, UpbBindT(StartString<T>, new StringHandlerData<T>(proto2_f, r))));
    }
  }

  // This needs to be templated because google3 string is not std::string.
  template <typename T>
  static T* StartString(goog::Message* m, const StringHandlerData<T>* data,
                        size_t size_hint) {
    UPB_UNUSED(size_hint);
    T** str = data->GetStringPointer(m);
    data->SetHasbit(m);
    // If it points to the default instance, we must create a new instance.
    if (*str == data->prototype()) *str = new T();
    (*str)->clear();
    // reserve() here appears to hurt performance rather than help.
    return *str;
  }

  template <typename T>
  static void OnStringBuf(T* str, const char* buf, size_t n) {
    str->append(buf, n);
  }

  template <typename T>
  static T* StartRepeatedString(goog::RepeatedPtrField<T>* r,
                                size_t size_hint) {
    UPB_UNUSED(size_hint);
    T* str = r->Add();
    str->clear();
    // reserve() here appears to hurt performance rather than help.
    return str;
  }

  // StringExtension ///////////////////////////////////////////////////////////

  template <typename T>
  static void SetStringExtensionHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    assert(proto2_f->is_extension());
    CHKRET(h->SetStringHandler(f, UpbMakeHandlerT(OnStringBuf<T>)));
    scoped_ptr<ExtensionFieldData> data(new ExtensionFieldData(proto2_f, r));
    if (f->IsSequence()) {
      CHKRET(h->SetStartStringHandler(
          f, UpbBindT(StartRepeatedStringExtension<T>, data.release())));
    } else {
      CHKRET(h->SetStartStringHandler(
          f, UpbBindT(StartStringExtension<T>, data.release())));
    }
  }

  // Templated because google3 is not std::string.
  template <class T>
  static T* StartStringExtension(goog::Message* m,
                                 const ExtensionFieldData* data,
                                 size_t size_hint) {
    UPB_UNUSED(size_hint);
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    return set->MutableString(data->number(), data->type(), NULL);
  }

  template <class T>
  static T* StartRepeatedStringExtension(goog::Message* m,
                                         const ExtensionFieldData* data,
                                         size_t size_hint) {
    UPB_UNUSED(size_hint);
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    return set->AddString(data->number(), data->type(), NULL);
  }

  // SubMessage ////////////////////////////////////////////////////////////////

  class SubMessageHandlerData : public FieldOffset {
   public:
    SubMessageHandlerData(const goog::FieldDescriptor* f,
                          const goog::internal::GeneratedMessageReflection* r,
                          const goog::Message* prototype)
        : FieldOffset(f, r), prototype_(prototype) {}

    const goog::Message* prototype() const { return prototype_; }

   private:
    const goog::Message* const prototype_;
  };

  static void SetSubMessageHandlers(
      const goog::FieldDescriptor* proto2_f, const goog::Message& m,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    const goog::Message* field_prototype = GetFieldPrototype(m, proto2_f);
    scoped_ptr<SubMessageHandlerData> data(
        new SubMessageHandlerData(proto2_f, r, field_prototype));
    if (f->IsSequence()) {
      SetStartRepeatedSubmessageField(proto2_f, r, f, h);
      CHKRET(h->SetStartSubMessageHandler(
          f, UpbBind(StartRepeatedSubMessage, data.release())));
    } else {
      CHKRET(h->SetStartSubMessageHandler(
          f, UpbBind(StartSubMessage, data.release())));
    }
  }

  static goog::Message* StartSubMessage(goog::Message* m,
                                        const SubMessageHandlerData* data) {
    data->SetHasbit(m);
    goog::Message** subm = data->GetFieldPointer<goog::Message*>(m);
    if (*subm == NULL || *subm == data->prototype()) {
      *subm = data->prototype()->New();
    }
    return *subm;
  }

  class RepeatedMessageTypeHandler {
   public:
    typedef goog::Message Type;
    // AddAllocated() calls this, but only if other objects are sitting
    // around waiting for reuse, which we will not do.
    static void Delete(Type* t) {
      UPB_UNUSED(t);
      assert(false);
    }
  };

  // Closure is a RepeatedPtrField<SubMessageType>*, but we access it through
  // its base class RepeatedPtrFieldBase*.
  static goog::Message* StartRepeatedSubMessage(
      goog::internal::RepeatedPtrFieldBase* r,
      const SubMessageHandlerData* data) {
    goog::Message* submsg = r->AddFromCleared<RepeatedMessageTypeHandler>();
    if (!submsg) {
      submsg = data->prototype()->New();
      r->AddAllocated<RepeatedMessageTypeHandler>(submsg);
    }
    return submsg;
  }

  // SubMessageExtension ///////////////////////////////////////////////////////

  class SubMessageExtensionHandlerData : public ExtensionFieldData {
   public:
    SubMessageExtensionHandlerData(
        const goog::FieldDescriptor* proto2_f,
        const goog::internal::GeneratedMessageReflection* r,
        const goog::Message* prototype)
        : ExtensionFieldData(proto2_f, r),
          prototype_(prototype) {
    }

    const goog::Message* prototype() const { return prototype_; }

   private:
    const goog::Message* const prototype_;
  };

  static void SetSubMessageExtensionHandlers(
      const goog::FieldDescriptor* proto2_f,
      const goog::Message& m,
      const goog::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f,
      upb::Handlers* h) {
    const goog::Message* field_prototype = GetFieldPrototype(m, proto2_f);
    scoped_ptr<SubMessageExtensionHandlerData> data(
        new SubMessageExtensionHandlerData(proto2_f, r, field_prototype));
    if (f->IsSequence()) {
      CHKRET(h->SetStartSubMessageHandler(
          f, UpbBind(StartRepeatedSubMessageExtension, data.release())));
    } else {
      CHKRET(h->SetStartSubMessageHandler(
          f, UpbBind(StartSubMessageExtension, data.release())));
    }
  }

  static goog::Message* StartRepeatedSubMessageExtension(
      goog::Message* m, const SubMessageExtensionHandlerData* data) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    // Because we found this message via a descriptor, we know it has a
    // descriptor and is therefore a Message and not a MessageLite.
    // Alternatively we could just use goog::MessageLite everywhere to avoid
    // this, but since they are in fact goog::Messages, it seems most clear
    // to refer to them as such.
    return CheckDownCast<goog::Message*>(set->AddMessage(
        data->number(), data->type(), *data->prototype(), NULL));
  }

  static goog::Message* StartSubMessageExtension(
      goog::Message* m, const SubMessageExtensionHandlerData* data) {
    goog::internal::ExtensionSet* set = data->GetExtensionSet(m);
    // See comment above re: this down cast.
    return CheckDownCast<goog::Message*>(set->MutableMessage(
        data->number(), data->type(), *data->prototype(), NULL));
  }

  // TODO(haberman): handle Unknown Fields.

#ifdef UPB_GOOGLE3
  // Handlers for types/features only included in internal proto2 release:
  // Cord, StringPiece, LazyField, and MessageSet.
  // TODO(haberman): LazyField, MessageSet.

  // Cord //////////////////////////////////////////////////////////////////////

  static void SetCordHandlers(
      const proto2::FieldDescriptor* proto2_f,
      const proto2::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    assert(!proto2_f->is_extension());
    CHKRET(h->SetStringHandler(f, UpbMakeHandler(&OnCordBuf)));
    if (f->IsSequence()) {
      SetStartRepeatedField<Cord>(proto2_f, r, f, h);
      CHKRET(h->SetStartStringHandler(f, UpbMakeHandler(StartRepeatedCord)));
    } else {
      CHKRET(h->SetStartStringHandler(
          f, UpbBind(StartCord, new FieldOffset(proto2_f, r))));
    }
  }

  static Cord* StartCord(goog::Message* m, const FieldOffset* offset,
                         size_t size_hint) {
    UPB_UNUSED(size_hint);
    offset->SetHasbit(m);
    Cord* field = offset->GetFieldPointer<Cord>(m);
    field->Clear();
    return field;
  }

  static void OnCordBuf(Cord* c, const char* buf, size_t n,
                        const upb::BufferHandle* handle) {
    const Cord* source_cord = handle->GetAttachedObject<Cord>();
    if (source_cord) {
      // This TODO is copied from CordReader::CopyToCord():
      // "We could speed this up by using CordReader internals."
      Cord piece(*source_cord);
      piece.RemovePrefix(handle->object_offset() + (buf - handle->buffer()));
      assert(piece.size() >= n);
      piece.RemoveSuffix(piece.size() - n);

      c->Append(piece);
    } else {
      c->Append(StringPiece(buf, n));
    }
  }

  static Cord* StartRepeatedCord(proto2::RepeatedField<Cord>* r,
                                 size_t size_hint) {
    UPB_UNUSED(size_hint);
    return r->Add();
  }

  // StringPiece ///////////////////////////////////////////////////////////////

  static void SetStringPieceHandlers(
      const proto2::FieldDescriptor* proto2_f,
      const proto2::internal::GeneratedMessageReflection* r,
      const upb::FieldDef* f, upb::Handlers* h) {
    assert(!proto2_f->is_extension());
    CHKRET(h->SetStringHandler(f, UpbMakeHandler(OnStringPieceBuf)));
    if (f->IsSequence()) {
      SetStartRepeatedPtrField<proto2::internal::StringPieceField>(proto2_f, r,
                                                                   f, h);
      CHKRET(h->SetStartStringHandler(
          f, UpbMakeHandler(StartRepeatedStringPiece)));
    } else {
      CHKRET(h->SetStartStringHandler(
          f, UpbBind(StartStringPiece, new FieldOffset(proto2_f, r))));
    }
  }

  static void OnStringPieceBuf(proto2::internal::StringPieceField* field,
                               const char* buf, size_t len) {
    // TODO(haberman): alias if possible and enabled on the input stream.
    // TODO(haberman): add a method to StringPieceField that lets us avoid
    // this copy/malloc/free.
    size_t new_len = field->size() + len;
    char* data = new char[new_len];
    memcpy(data, field->data(), field->size());
    memcpy(data + field->size(), buf, len);
    field->CopyFrom(StringPiece(data, new_len));
    delete[] data;
  }

  static proto2::internal::StringPieceField* StartStringPiece(
      goog::Message* m, const FieldOffset* offset, size_t size_hint) {
    UPB_UNUSED(size_hint);
    offset->SetHasbit(m);
    proto2::internal::StringPieceField* field =
        offset->GetFieldPointer<proto2::internal::StringPieceField>(m);
    field->Clear();
    return field;
  }

  static proto2::internal::StringPieceField* StartRepeatedStringPiece(
      proto2::RepeatedPtrField<proto2::internal::StringPieceField>* r,
      size_t size_hint) {
    UPB_UNUSED(size_hint);
    proto2::internal::StringPieceField* field = r->Add();
    field->Clear();
    return field;
  }

#endif  // UPB_GOOGLE3
};

namespace upb {
namespace google {

bool TrySetWriteHandlers(const goog::FieldDescriptor* proto2_f,
                         const goog::Message& prototype,
                         const upb::FieldDef* upb_f, upb::Handlers* h) {
  return me::GMR_Handlers::TrySet(proto2_f, prototype, upb_f, h);
}

const goog::Message* GetFieldPrototype(const goog::Message& m,
                                       const goog::FieldDescriptor* f) {
  return me::GMR_Handlers::GetFieldPrototype(m, f);
}

}  // namespace google
}  // namespace upb
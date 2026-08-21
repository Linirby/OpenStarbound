#include "StarRenderingLuaBindings.hpp"
#include "StarJsonExtra.hpp"
#include "StarLuaConverters.hpp"
#include "StarClientApplication.hpp"
#include "StarRenderer.hpp"

#include "StarByteArray.hpp"

namespace Star {

namespace {

void packStd140Value(ByteArray& out, size_t& offset, LuaValue const& val);

void writeAligned(ByteArray& out, size_t& offset, void const* data, size_t size, size_t alignment) {
  offset = (offset + (alignment - 1)) & ~(alignment - 1);
  out.writeFrom((char const*)data, offset, size);
  offset += size;
}

bool isNumericOrBool(LuaValue const& val) {
  return val.is<LuaFloat>() || val.is<LuaInt>() || val.is<LuaBoolean>();
}

float toFloat(LuaValue const& val) {
  if (auto f = val.ptr<LuaFloat>())
    return (float)*f;
  if (auto i = val.ptr<LuaInt>())
    return (float)*i;
  if (auto b = val.ptr<LuaBoolean>())
    return *b ? 1.0f : 0.0f;
  return 0.0f;
}

bool isAllNumericOrBool(LuaTable const& table, size_t len) {
  for (size_t i = 1; i <= len; ++i) {
    if (!isNumericOrBool(table.get(i)))
      return false;
  }
  return true;
}

void packStd140Table(ByteArray& out, size_t& offset, LuaTable const& table) {
  size_t len = (size_t)max<LuaInt>(0, table.length());
  if (len == 0)
    return;

  if (isAllNumericOrBool(table, len)) {
    if (len == 2) {
      float v[2] = { toFloat(table.get(1)), toFloat(table.get(2)) };
      writeAligned(out, offset, v, sizeof(v), 8);
      return;
    } else if (len == 3) {
      float v[3] = { toFloat(table.get(1)), toFloat(table.get(2)), toFloat(table.get(3)) };
      writeAligned(out, offset, v, sizeof(v), 16);
      return;
    } else if (len == 4) {
      float v[4] = { toFloat(table.get(1)), toFloat(table.get(2)), toFloat(table.get(3)), toFloat(table.get(4)) };
      writeAligned(out, offset, v, sizeof(v), 16);
      return;
    } else if (len == 9) {
      offset = (offset + 15) & ~15;
      for (size_t col = 0; col < 3; ++col) {
        float colV[3] = {
          toFloat(table.get(col * 3 + 1)),
          toFloat(table.get(col * 3 + 2)),
          toFloat(table.get(col * 3 + 3))
        };
        writeAligned(out, offset, colV, sizeof(colV), 16);
        offset = (offset + 15) & ~15;
      }
      return;
    } else if (len == 16) {
      float v[16];
      for (size_t i = 0; i < 16; ++i)
        v[i] = toFloat(table.get(i + 1));
      writeAligned(out, offset, v, sizeof(v), 16);
      return;
    }
  }

  if (len == 3) {
    auto c1 = table.get(1).ptr<LuaTable>();
    auto c2 = table.get(2).ptr<LuaTable>();
    auto c3 = table.get(3).ptr<LuaTable>();
    if (c1 && c2 && c3 && c1->length() == 3 && c2->length() == 3 && c3->length() == 3 &&
        isAllNumericOrBool(*c1, 3) && isAllNumericOrBool(*c2, 3) && isAllNumericOrBool(*c3, 3)) {
      offset = (offset + 15) & ~15;
      for (size_t col = 1; col <= 3; ++col) {
        LuaTable colT = table.get(col).get<LuaTable>();
        float colV[3] = { toFloat(colT.get(1)), toFloat(colT.get(2)), toFloat(colT.get(3)) };
        writeAligned(out, offset, colV, sizeof(colV), 16);
        offset = (offset + 15) & ~15;
      }
      return;
    }
  } else if (len == 4) {
    auto c1 = table.get(1).ptr<LuaTable>();
    auto c2 = table.get(2).ptr<LuaTable>();
    auto c3 = table.get(3).ptr<LuaTable>();
    auto c4 = table.get(4).ptr<LuaTable>();
    if (c1 && c2 && c3 && c4 && c1->length() == 4 && c2->length() == 4 && c3->length() == 4 && c4->length() == 4 &&
        isAllNumericOrBool(*c1, 4) && isAllNumericOrBool(*c2, 4) && isAllNumericOrBool(*c3, 4) && isAllNumericOrBool(*c4, 4)) {
      offset = (offset + 15) & ~15;
      for (size_t col = 1; col <= 4; ++col) {
        LuaTable colT = table.get(col).get<LuaTable>();
        float colV[4] = { toFloat(colT.get(1)), toFloat(colT.get(2)), toFloat(colT.get(3)), toFloat(colT.get(4)) };
        writeAligned(out, offset, colV, sizeof(colV), 16);
      }
      return;
    }
  }

  // Array / struct elements in std140
  offset = (offset + 15) & ~15;
  for (size_t i = 1; i <= len; ++i) {
    offset = (offset + 15) & ~15;
    packStd140Value(out, offset, table.get(i));
    offset = (offset + 15) & ~15;
  }
}

void packStd140Value(ByteArray& out, size_t& offset, LuaValue const& val) {
  if (auto b = val.ptr<LuaBoolean>()) {
    int32_t v = *b ? 1 : 0;
    writeAligned(out, offset, &v, sizeof(v), 4);
  } else if (auto i = val.ptr<LuaInt>()) {
    int32_t v = (int32_t)*i;
    writeAligned(out, offset, &v, sizeof(v), 4);
  } else if (auto f = val.ptr<LuaFloat>()) {
    float v = (float)*f;
    writeAligned(out, offset, &v, sizeof(v), 4);
  } else if (auto s = val.ptr<LuaString>()) {
    out.writeFrom(s->ptr(), offset, s->length());
    offset += s->length();
  } else if (auto t = val.ptr<LuaTable>()) {
    packStd140Table(out, offset, *t);
  }
}

ByteArray packStd140(LuaValue const& val) {
  ByteArray out;
  size_t offset = 0;
  if (auto s = val.ptr<LuaString>()) {
    out.append(s->ptr(), s->length());
  } else if (auto t = val.ptr<LuaTable>()) {
    size_t len = (size_t)max<LuaInt>(0, t->length());
    for (size_t i = 1; i <= len; ++i) {
      packStd140Value(out, offset, t->get(i));
    }
  } else {
    packStd140Value(out, offset, val);
  }
  return out;
}

}

LuaCallbacks LuaBindings::makeRenderingCallbacks(ClientApplication* app) {
  LuaCallbacks callbacks;
  
  // if the last argument is defined and true, this change will also be saved to starbound.config and read on next game start, use for things such as an interface that does this
  callbacks.registerCallbackWithSignature<unsigned>("framesSkipped", bind(mem_fn(&ClientApplication::framesSkipped), app));
  callbacks.registerCallbackWithSignature<void, String, bool, Maybe<bool>>("setPostProcessGroupEnabled", bind(mem_fn(&ClientApplication::setPostProcessGroupEnabled), app, _1, _2, _3));
  callbacks.registerCallbackWithSignature<bool, String>("postProcessGroupEnabled", bind(mem_fn(&ClientApplication::postProcessGroupEnabled), app, _1));
  
  
  // not entirely necessary (root.assetJson can achieve the same purpose) but may as well
  callbacks.registerCallbackWithSignature<Json>("postProcessGroups", bind(mem_fn(&ClientApplication::postProcessGroups), app));
  
  // typedef Variant<float, int, Vec4F, Vec3F, Vec2F, bool> RenderEffectParameter;
  // TODO: maybe we should be checking the effect's type and converting lua based on that instead of converting to a Variant and relying on the Variant's ordering
  // specifically checks if the effect parameter is an int since Lua prefers converting the values to floats
  callbacks.registerCallback("setEffectParameter", [app](String const& effectName, String const& effectParameter, RenderEffectParameter const& value) {
    auto renderer = app->renderer();
    auto mtype = renderer->getEffectScriptableParameterType(effectName, effectParameter);
    if (mtype) {
      auto type = mtype.value();
      if (type == 1 && value.is<float>()) {
        renderer->setEffectScriptableParameter(effectName, effectParameter, (int)value.get<float>());
      } else {
        renderer->setEffectScriptableParameter(effectName, effectParameter, value);
      }
    }
  });
  
  callbacks.registerCallback("getEffectParameter", [app](String const& effectName, String const& effectParameter) {
    auto renderer = app->renderer();
    return renderer->getEffectScriptableParameter(effectName, effectParameter);
  });

  callbacks.registerCallback("setEffectUniformBuffer", [app](String const& effectName, String const& blockName, LuaValue const& data) {
    if (auto renderer = app->renderer()) {
      if (auto s = data.ptr<LuaString>()) {
        renderer->setEffectUniformBuffer(effectName, blockName, s->ptr(), s->length());
      } else {
        ByteArray buffer = packStd140(data);
        renderer->setEffectUniformBuffer(effectName, blockName, buffer.ptr(), buffer.size());
      }
    }
  });

  callbacks.registerCallback("setEffectUniformBufferData", [app](String const& effectName, String const& blockName, LuaValue const& data) {
    if (auto renderer = app->renderer()) {
      if (auto s = data.ptr<LuaString>()) {
        renderer->setEffectUniformBuffer(effectName, blockName, s->ptr(), s->length());
      } else {
        ByteArray buffer = packStd140(data);
        renderer->setEffectUniformBuffer(effectName, blockName, buffer.ptr(), buffer.size());
      }
    }
  });
  
  // not saved; should be loaded by Lua again
  callbacks.registerCallbackWithSignature<void, String, unsigned>("setPostProcessLayerPasses", bind(mem_fn(&ClientApplication::setPostProcessLayerPasses), app, _1, _2));

  return callbacks;
}


}

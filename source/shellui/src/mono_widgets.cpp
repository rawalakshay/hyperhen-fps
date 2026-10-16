/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Mono helpers the game monitor bar needs. Kept apart from mono_runtime.cpp
 * so the standalone FPS monitor can link them without Toolbox globals.
 */

#include "hooked_funcs.hpp"
#include "external_symbols.hpp"
#include "ipc.hpp" // LOG_*
#include <string>

MonoImage * getDLLimage(const char* dll_file){
  
  std::string dll_path = "/system_ex/common_ex/lib/" + std::string(dll_file);
  MonoAssembly * Assembly = mono_domain_assembly_open(Root_Domain, dll_path.c_str());
  if (!Assembly) {
    LOG_ERROR("Failed to open assembly %s.", dll_path.c_str());
    return nullptr;
  }

  MonoImage * img = mono_assembly_get_image(Assembly);
  if (!img) {
    LOG_ERROR("Failed to get image %s.", dll_path.c_str());
    return nullptr;
  }
  return img;
}


MonoObject* New_Object(MonoClass* Klass)
{
    if (Klass == nullptr)
    {
        return nullptr;
    }

    return mono_object_new(Root_Domain, Klass);
}

MonoObject* CreateUIColor(float r, float g, float b, float a)
{
    MonoClass* uIColor = mono_class_from_name(pui_img, "Sce.PlayStation.PUI", "UIColor");

    // Allocates memory for our new instance of a class.
    MonoObject* uIColorInstance = New_Object(uIColor);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIColorInstance);

    Invoke<void>(pui_img, uIColor, realInstance, ".ctor", r, g, b, a);

    return realInstance;
}

MonoObject* CreateUIFont(int size, int style, int weight)
{
    MonoClass* uIFont = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "UIFont");

    // Allocates memory for our new instance of a class.
    MonoObject* uIFontInstance = New_Object(uIFont);
    MonoObject* realInstance = (MonoObject*)mono_object_unbox(uIFontInstance);

    Invoke<void>(pui_img, uIFont, realInstance, ".ctor", size, style, weight);

    return realInstance;
}

MonoObject* CreateLabel(const char* name, float x, float y, const char* text, MonoObject* font, int horzAlign, int vertAlign, float r, float g, float b, float a)
{
    MonoClass* labelClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Label");

    // Allocates memory for our new instance of a class.
    MonoObject* labelInstance = New_Object(labelClass);

    // Call Constructor.
    mono_runtime_object_init(labelInstance);

    Set_Property(labelClass, labelInstance, "Name", mono_string_new(Root_Domain, name));
    Set_Property(labelClass, labelInstance, "PositionType", 1);
    Set_Property(labelClass, labelInstance, "MarginLeft", x);
    Set_Property(labelClass, labelInstance, "MarginTop", y);
    Set_Property(labelClass, labelInstance, "Text", mono_string_new(Root_Domain, text));
    Set_Property_Invoke(labelClass, labelInstance, "Font", font);
    Set_Property(labelClass, labelInstance, "HorizontalAlignment", horzAlign);
    Set_Property(labelClass, labelInstance, "VerticalAlignment", vertAlign);
    Set_Property_Invoke(labelClass, labelInstance, "TextColor", CreateUIColor(r, g, b, a));

    Set_Property(labelClass, labelInstance, "FitWidthToText", false);
    Set_Property(labelClass, labelInstance, "FitHeightToText", true);
    Set_Property(labelClass, labelInstance, "NumberOfLines", 1);
    /* Readable HUD text on dark bar. */
    Set_Property(labelClass, labelInstance, "EnableThemedTextShadow", true);

    return labelInstance;
}

void Widget_Append_Child(MonoObject* widget, MonoObject* child)
{
    MonoClass* widgetClass = mono_class_from_name(pui_img, "Sce.PlayStation.PUI.UI2", "Widget");
    MonoMethod* appendChild = mono_class_get_method_from_name(widgetClass, "AppendChild", 1);

    void* args[1];
    args[0] = child;

    mono_runtime_invoke(appendChild, widget, args, nullptr);
}

uint64_t Get_Address_of_Method(MonoImage *Assembly_Image, const char *Name_Space, const char *Class_Name, const char *Method_Name, int Param_Count)
{
  MonoClass *klass = mono_class_from_name(Assembly_Image, Name_Space, Class_Name);
  if (!klass)
  {
    LOG_ERROR("Get_Address_of_Method: failed to open class \"%s\" in namespace \"%s\"", Class_Name, Name_Space);
    return 0;
  }

  MonoMethod *Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);
  if (!Method)
  {
    LOG_ERROR("Get_Address_of_Method: failed to find method \"%s\" in class \"%s\"", Method_Name, Class_Name);
    return 0;
  }

  // return (uint64_t)mono_aot_get_method(Root_Domain, Method);
  return mono_compile_method(Method);
}

uint64_t Get_Address_of_Method(MonoImage* Assembly_Image, MonoClass* klass, const char* Method_Name, int Param_Count)
{
	if (!klass)
	{
		return 0;
	}

	MonoMethod* Method = mono_class_get_method_from_name(klass, Method_Name, Param_Count);

	if (!Method)
	{
		return 0;
	}

	//return (uint64_t)mono_aot_get_method(mono_get_root_domain(), Method);
  return mono_compile_method(Method);
}

#ifndef _TUTK_JSONAPIs_H_
#define _TUTK_JSONAPIs_H_

#include <stdint.h>

#ifdef _WIN32
    /** @cond */
    #ifdef IOTC_STATIC_LIB
    #define TUTK_JSON_API
    #elif defined AVAPI_EXPORTS
    #define TUTK_JSON_API __declspec(dllexport)
    #else
    #define TUTK_JSON_API __declspec(dllimport)
    #endif // #ifdef P2PAPI_EXPORTS
    /** @endcond */
#else // #ifdef _WIN32
	#define TUTK_JSON_API
#endif //#ifdef _WIN32

#if defined(__GNUC__) || defined(__clang__)
    #define TUTK_JSON_API_DEPRECATED __attribute__((deprecated))
    #elif defined(_MSC_VER)
    #ifdef IOTC_STATIC_LIB
    #define TUTK_JSON_API_DEPRECATED __declspec(deprecated)
        #elif defined P2PAPI_EXPORTS
    #define TUTK_JSON_API_DEPRECATED __declspec(deprecated, dllexport)
    #else
    #define TUTK_JSON_API_DEPRECATED __declspec(deprecated, dllimport)
    #endif
#else
    #define TUTK_JSON_API_DEPRECATED
#endif

#ifndef _WIN32
#define __stdcall
#endif // #ifndef _WIN32

/* ============================================================================
 * Error Code Declaration
 * ============================================================================
 */
/** The function is performed successfully. */
#define    TUTK_JSON_ER_NoERROR                       0

/** The passed-in arguments for the function are incorrect */
#define    TUTK_JSON_ER_INVALID_ARG                   -40002

/** Insufficient memory for allocation */
#define    TUTK_JSON_ER_MEM_INSUFFICIENT              -40006

/** JSON object is not this type*/
#define    TUTK_JSON_ER_OBJ_TYPE_ERROR                -43001

/** JSON object add data error*/
#define    TUTK_JSON_ER_OBJ_ADD_ERROR                 -43002

/** object is not exist in JSON*/
#define    TUTK_JSON_ER_OBJ_NOT_EXIST                 -43003

/** JSON array operate error*/
#define    TUTK_JSON_ER_OBJ_ARRAY_ERROR               -43004


/* ============================================================================
 * Platform Dependant Macro Definition
 * ============================================================================
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ============================================================================
 * Enumeration Declaration
 * ============================================================================
 */

typedef enum TUTKJsonType {
  JSON_TYPE_NULL,
  JSON_TYPE_BOOL,
  JSON_TYPE_DOUBLE,
  JSON_TYPE_INT,
  JSON_TYPE_OBJECT,
  JSON_TYPE_ARRAY,
  JSON_TYPE_STRING
} TUTKJsonType;

/* ============================================================================
 * Type Definition
 * ============================================================================
 */

typedef struct TUTKJsonObject TUTKJsonObject;

/**
 * \brief New a empty TUTKJsonObject
 *
 * \details Create a new TUTKJsonObject which type is JSON_TYPE_OBJECT, but empty.
 *
 * \return TUTKJsonObject
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_Empty_Obj(void);

/**
 * \brief 
 *
 * \details Create a new TUTKJsonObject of type JSON_TYPE_BOOL
 *
 * \param bool_value [in] 
 *
 * \return TUTKJsonObject which type is JSON_TYPE_BOOL.
 * 
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_Bool(const int32_t bool_value);

/**
 * \brief 
 *
 * \details Create a new TUTKJsonObject of type JSON_TYPE_DOUBLE
 *
 * \param double_value [in] 
 *
 * \return TUTKJsonObject which type is JSON_TYPE_DOUBLE.
 * 
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_Double(const double double_value);

/**
 * \brief 
 *
 * \details Create a new TUTKJsonObject of type JSON_TYPE_INT
 *
 * \param int_value [in] 
 *
 * \return TUTKJsonObject which type is JSON_TYPE_INT.
 * 
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_Int(const int32_t int_value);

/**
 * \brief 
 *
 * \details Create a new TUTKJsonObject of type JSON_TYPE_STRING
 *
 * \param string_value [in] 
 *
 * \return TUTKJsonObject which type is JSON_TYPE_STRING.
 * 
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_String(const char *string_value);

/**
 * \brief 
 *
 * \details Add an object field to the parent_obj of type JSON_TYPE_OBJECT
 *
 * \param parent_obj [in] 
 * \param key [in] 
 * \param value_obj [in] 
 *
 * \return 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Obj_Add(const TUTKJsonObject *parent_obj, const char *key, const TUTKJsonObject *value_obj);

/**
 * \brief 
 *
 * \details Remove the given TUTKJsonObject field
 *
 * \param json_obj [in] 
 * \param key [in] 
 *
 * \return 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Obj_Remove(const TUTKJsonObject *json_obj, const char *key);

/**
 * \brief 
 *
 * \details Create a new TUTKJsonObject of type JSON_TYPE_ARRAY
 *
 *
 * \return 
 * 
 */
TUTK_JSON_API TUTKJsonObject *TUTK_Json_Obj_New_Empty_Array(void);

/**
 * \brief 
 *
 * \details Add an element to the end of a TUTKJsonObject of type JSON_TYPE_ARRAY
 *
 * \param array_obj [in] 
 * \param value_obj [in] 
 *
 * \return 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Array_Add(const TUTKJsonObject *array_obj, const TUTKJsonObject *value_obj);

/**
 * \brief 
 *
 * \details Insert an element at a specified index in an array (a TUTKJsonObject of type JSON_TYPE_ARRAY)
 *
 * \param array_obj [in] 
 * \param index [in] 
 * \param value_obj [in] 
 *
 * \return 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Array_Insert(const TUTKJsonObject *array_obj, int32_t index, const TUTKJsonObject *value_obj);

/**
 * \brief 
 *
 * \details Delete an elements from a specified index in an array (a TUTKJsonObject of type JSON_TYPE_ARRAY)
 *
 * \param array_obj [in] 
 * \param index [in] 
 * \param count [in] 
 *
 * \return 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Array_Delete(const TUTKJsonObject *array_obj, int32_t index, int32_t count);

/**
 * \brief New a TUTKJsonObject from vaild JSON string.
 *
 * \details Parse a string and return a non-NULL json_object if a valid JSON value is found.
 *
 * \param json_str [in] String of vaild JSON.
 * \param json_obj [out] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 *
 * \return #TUTK_ER_NoERROR if create successfully
 * \return Error code if return value < 0
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR json_str is not a JSON format string
 *
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Create_From_String(const char *json_str, TUTKJsonObject **json_obj);

/**
 * \brief Free a root TUTKJsonObject
 *
 * \details Free a root TUTKJsonObject.Make sure to free the TUTKJsonObject that ceated by 
 *          TUTK_Json_Obj_New_Empty_Obj or TUTK_Json_Obj_Create_From_String or it may cause
 *          memory leak.
 *
 * \param root_obj [in] root TUTKJsonObject of JSON.
 *
 * \return #TUTK_ER_NoERROR if freed successfully
 * \return Error code if return value < 0
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *
 * \see TUTK_Json_Obj_Create_From_String(), TUTK_Json_Obj_New_Empty_Obj()
 * 
 * \attention This function may crash when input is not a root object.
 *
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Release(TUTKJsonObject *root_obj);

/**
 * \brief Get object count of a JSON_TYPE_OBJECT TUTKJsonObject
 *
 * \details Get the size of an object in terms of the number of fields it has.
 *
 * \param json_obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 *
 * \return >=0 for object count
 * \return Error code if return value < 0
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR root_obj is not a JSON_TYPE_OBJECT TUTKJsonObject
 *
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_length(const TUTKJsonObject *json_obj);

/**
 * \brief Get sub TUTKJsonObject by specific key
 *
 * \details Get sub TUTKJsonObject of a JSON_TYPE_OBJECT TUTKJsonObject with specific key.
 *
 * \param parent_obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 * \param key [in] Key name of target pair.
 * \param value_obj [out] Sub TUTKJsonObject of target pair.
 *
 * \return #TUTK_ER_NoERROR when get sub TUTKJsonObject successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR parent_obj is not a JSON_TYPE_OBJECT TUTKJsonObject
 *			- #TUTK_ER_JSON_OBJ_NOT_EXIST Key name is not exist
 * 
 * \attention value_obj is constant data , do not modify or free value_obj.
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Sub_Obj(const TUTKJsonObject *parent_obj, const char *key, const TUTKJsonObject **value_obj);

/**
 * \brief Get the type of the TUTKJsonObject.
 *
 * \details Get TUTKJsonType of input TUTKJsonObject.
 *
 * \param value_obj [in] Any type of TUTKJsonObject.
 *
 * \return >=0 for TUTKJsonType
 *			- #TUTK_ER_INVALID_ARG Input null argument
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Type(const TUTKJsonObject *value_obj);

/**
 * \brief Get boolean value from TUTKJsonObject
 *
 * \details Get boolean value from JSON_TYPE_BOOL type TUTKJsonObject
 *
 * \param value_obj [in] A TUTKJsonObject which type is JSON_TYPE_BOOL.
 * \param bool_value [out] Boolean value of TUTKJsonObject. 0 for FALSE, 1 for TRUE.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR value_obj is not a JSON_TYPE_BOOL TUTKJsonObject
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Bool(const TUTKJsonObject *value_obj, int32_t *bool_value);

/**
 * \brief Get double value from TUTKJsonObject
 *
 * \details Get double value from JSON_TYPE_DOUBLE type TUTKJsonObject
 *
 * \param value_obj [in] A TUTKJsonObject which type is JSON_TYPE_DOUBLE.
 * \param double_value [out] Double value of TUTKJsonObject.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR value_obj is not a JSON_TYPE_DOUBLE TUTKJsonObject
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Double(const TUTKJsonObject *value_obj, double *double_value);

/**
 * \brief Get integer value from TUTKJsonObject
 *
 * \details Get integer value from JSON_TYPE_INT type TUTKJsonObject
 *
 * \param value_obj [in] A TUTKJsonObject which type is JSON_TYPE_INT.
 * \param int_value [out] Integer value of TUTKJsonObject.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR value_obj is not a JSON_TYPE_INT TUTKJsonObject
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Int(const TUTKJsonObject *value_obj, int32_t *int_value);

/**
 * \brief Get string from TUTKJsonObject
 *
 * \details Get string from JSON_TYPE_STRING type TUTKJsonObject
 *
 * \param value_obj [in] A TUTKJsonObject which type is JSON_TYPE_STRING.
 * \param str_value [out] String of TUTKJsonObject.
 *
 * \return #TUTK_ER_NoERROR when get string successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 * 
 * \attention str_value is constant data , do not modify or free str_value. 
 *            value_obj can be other type, not only JSON_TYPE_STRING
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_String(const TUTKJsonObject *value_obj, const char **str_value);

/**
 * \brief Get array length from TUTKJsonObject
 *
 * \details Get array length from JSON_TYPE_ARRAY type TUTKJsonObject 
 *
 * \param array_obj [in] A TUTKJsonObject which type is JSON_TYPE_ARRAY.
 *
 * \return >=0 for array length
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR value_obj is not a JSON_TYPE_ARRAY TUTKJsonObject
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Array_length(const TUTKJsonObject *array_obj);

/**
 * \brief Get sub TUTKJsonObject from JSON_TYPE_ARRAY type TUTKJsonObject
 *
 * \details Get sub TUTKJsonObject of specificed index from JSON_TYPE_ARRAY type TUTKJsonObject
 *
 * \param array_obj [in] A TUTKJsonObject which type is JSON_TYPE_ARRAY.
 * \param index [in] TUTKJsonObject index of array.
 * \param value_obj [out] Sub TUTKJsonObject of index.
 *
 * \return #TUTK_ER_NoERROR when get sub TUTKJsonObject successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR array_obj is not a JSON_TYPE_ARRAY TUTKJsonObject
 *			- #TUTK_ER_JSON_OBJ_NOT_EXIST Key index is not exist
 * 
 * \attention value_obj is constant data , do not modify or free value_obj. 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Array_Element(const TUTKJsonObject *array_obj, int32_t index, const TUTKJsonObject **value_obj);

/**
 * \brief Stringify TUTKJsonObject to string
 *
 * \details Get string from any type TUTKJsonObject
 *
 * \param json_obj [in] Any type of TUTKJsonObject.
 *
 * \return String of json_obj
 * 
 * \attention return string is constant data , do not modify or free string 
 * 
 */
TUTK_JSON_API const char *TUTK_Json_Obj_To_String(const TUTKJsonObject *json_obj);

/**
 * \brief Get specific boolean value from JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \details Get boolean value by key name of JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \param obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 * \param key [in] Key name of target pair.
 * \param bool_value [out] Boolean value of Key. 0 for FALSE, 1 for TRUE.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR Value of key type is not boolean
 *
 * \see TUTK_Json_Obj_Get_Sub_Obj(), TUTK_Json_Obj_Get_Bool()
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Sub_Obj_Bool(const TUTKJsonObject *obj, const char *key, int32_t *bool_value);

/**
 * \brief Get specific double value from JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \details Get double value by key name of JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \param obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 * \param key [in] Key name of target pair.
 * \param double_value [out] Double value of key.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR Value of key type is not double
 *
 * \see TUTK_Json_Obj_Get_Sub_Obj(), TUTK_Json_Obj_Get_Double()
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Sub_Obj_Double(const TUTKJsonObject *obj, const char *key, double *double_value);

/**
 * \brief Get specific integer value from JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \details Get integer value by key name of JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \param obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 * \param key [in] Key name of target pair.
 * \param int_value [out] Integer value of key.
 *
 * \return #TUTK_ER_NoERROR when get value successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR Value of key type is not integer
 *
 * \see TUTK_Json_Obj_Get_Sub_Obj(), TUTK_Json_Obj_Get_Int()
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Sub_Obj_Int(const TUTKJsonObject *obj, const char *key, int32_t *int_value);

/**
 * \brief Get specific string from JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \details  Get string by key name of JSON_TYPE_OBJECT type TUTKJsonObject
 *
 * \param obj [in] A TUTKJsonObject which type is JSON_TYPE_OBJECT.
 * \param key [in] Key name of target pair.
 * \param str_value [out] String of key.
 *
 * \return #TUTK_ER_NoERROR when get string successfully
 *			- #TUTK_ER_INVALID_ARG Input null argument
 *			- #TUTK_ER_JSON_OBJ_TYPE_ERROR Value of key type is not string
 * 
 * \see TUTK_Json_Obj_Get_Sub_Obj(), TUTK_Json_Obj_Get_String()
 * 
 * \attention str_value is constant data , do not modify or free str_value 
 * 
 */
TUTK_JSON_API int32_t TUTK_Json_Obj_Get_Sub_Obj_String(const TUTKJsonObject *obj, const char *key, const char **str_value);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _TUTK_JSONAPIs_H_ */

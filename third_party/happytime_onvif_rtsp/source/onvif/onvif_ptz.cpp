/***************************************************************************************
 *
 *  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
 *
 *  By downloading, copying, installing or using the software you agree to this license.
 *  If you do not agree to this license, do not download, install, 
 *  copy or use the software.
 *
 *  Copyright (C) 2014-2025, Happytimesoft Corporation, all rights reserved.
 *
 *  Redistribution and use in binary forms, with or without modification, are permitted.
 *
 *  Unless required by applicable law or agreed to in writing, software distributed 
 *  under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 *  CONDITIONS OF ANY KIND, either express or implied. See the License for the specific
 *  language governing permissions and limitations under the License.
 *
****************************************************************************************/

#include "sys_inc.h"
#include "onvif_ptz.h"
#include "onvif_utils.h"
#include "onvif_event.h"

#ifdef MEDIA2_SUPPORT
#include "onvif_media2.h"
#endif

#ifdef PTZ_SUPPORT

/***************************************************************************************/
extern ONVIF_CLS g_onvif_cls;
extern ONVIF_CFG g_onvif_cfg;
extern ONVIF_IDX g_onvif_idx;

/************************************************************************************
 *      
 * The typical sequence of events is that first a client requests a certain preset. 
 * When the device accepts this request, it will send out an invoked event. 
 * The invoked event has to follow either a reached event or an aborted event. 
 * The former is used when the PTZ unit was able to reach the invoked preset position, 
 * the latter in any other case. A reached event has to follow a left event, 
 * as soon as the PTZ unit moves away from the preset position
 *
*************************************************************************************/
void onvif_PTZPresetsInvokedNotify(const char * token, onvif_PTZPreset * p_preset)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:PTZController/PTZPresets/Invoked", PropertyOperation_Changed, 
        "PTZConfigurationToken", token, NULL, NULL, 
        "PresetToken", p_preset->token, "PresetName", p_preset->Name);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

void onvif_PTZPresetsReachedNotify(const char * token, onvif_PTZPreset * p_preset)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:PTZController/PTZPresets/Reached", PropertyOperation_Changed, 
        "PTZConfigurationToken", token, NULL, NULL, 
        "PresetToken", p_preset->token, "PresetName", p_preset->Name);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

void onvif_PTZPresetsAbortedNotify(const char * token, onvif_PTZPreset * p_preset)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:PTZController/PTZPresets/Aborted", PropertyOperation_Changed, 
        "PTZConfigurationToken", token, NULL, NULL, 
        "PresetToken", p_preset->token, "PresetName", p_preset->Name);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

void onvif_PTZPresetsLeftNotify(const char * token, onvif_PTZPreset * p_preset)
{
    NotificationMessageList * p_message = onvif_init_NotificationMessage3(
        "tns1:PTZController/PTZPresets/Left", PropertyOperation_Changed, 
        "PTZConfigurationToken", token, NULL, NULL, 
        "PresetToken", p_preset->token, "PresetName", p_preset->Name);
    if (p_message)
    {
        onvif_put_NotificationMessage(p_message);
    }
}

/***************************************************************************************/

/**
 * @brief
 *  A PTZ-capable device shall be able to report its PTZ status through 
 *  the GetStatus command.
 *
 *  The PTZ status contains the following information:
 *
 *  Position (optional) - Specifies the absolute position of the PTZ unit
 *  together with the space references. The default absolute spaces of the
 *  corresponding PTZ configuration shall be referenced within the position
 *  element. This information shall be present if the device signals support
 *  via the capability StatusPosition.
 *
 *  MoveStatus (optional) - Indicates if the pan/tilt/zoom device unit is 
 *  currently moving, idle or in an unknown state. This information shall be
 *  present if the device signals support via the capability MoveStatus. 
 *  The state Unknown shall not be used during normal operation, but is 
 *  reserved to initialization or error conditions.
 *
 *  Error (optional) - States a current PTZ error condition. This field
 *  shall be present if the MoveStatus signals Unkown.
 *
 *  UTC Time - Specifies the UTC time when this status was generated.
 *  
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_NoStatus
 **/
ONVIF_RET onvif_ptz_GetStatus(ONVIF_PROFILE * p_profile, onvif_PTZStatus * p_ptz_status)
{
    if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    // todo : add get ptz status code ...
    
    p_ptz_status->PositionFlag = 1;
    p_ptz_status->Position.PanTiltFlag = 1;
    p_ptz_status->Position.PanTilt.x = 0;
    p_ptz_status->Position.PanTilt.y = 0;
    p_ptz_status->Position.ZoomFlag = 1;
    p_ptz_status->Position.Zoom.x = 0;
    
    p_ptz_status->MoveStatusFlag = 1;
    p_ptz_status->MoveStatus.PanTiltFlag = 1;
    p_ptz_status->MoveStatus.PanTilt = MoveStatus_IDLE;
    p_ptz_status->MoveStatus.ZoomFlag = 1;
    p_ptz_status->MoveStatus.Zoom = MoveStatus_IDLE;

    p_ptz_status->ErrorFlag = 0;
    p_ptz_status->UtcTime = time(NULL);    
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A PTZ-capable device shall support continuous movements. The velocity 
 *  argument of this command specifies a signed speed value for the pan, 
 *  tilt and zoom. The combined pan/tilt element is optional and the Zoom
 *  element itself is optional. If the pan/tilt element is omitted, the 
 *  current pan/tilt movement shall not be affected by this command. The 
 *  same holds for the zoom element. The spaces referenced within the 
 *  velocity element shall be velocity spaces supported by the PTZ node.
 *  If the space information is omitted for the velocity argument, the 
 *  corresponding default spaces of the PTZ configuration belonging to 
 *  the specified media profile is used. A device may support continuous 
 *  pan/tilt movements and/or continuous zoom movements by providing only
 *  velocity spaces for the supported cases.
 *
 *  An existing timeout argument overrides the DefaultPTZTimeout parameter
 *  of the corresponding PTZ configuration for this Move operation. 
 *  The timeout parameter specifies how long the PTZ node continues to move.
 *
 *  A device shall stop movement in a particular axis (Pan, Tilt, or Zoom) 
 *  when zero is sent as the ContinuousMove parameter for that axis. Stopping
 *  shall have the same effect independent of the velocity space referenced.
 *
 *  If the requested velocity leads to absolute positions which cannot be 
 *  reached, the PTZ node shall move to a reachable position along the border
 *  of its range. A typical application of the continuous move operation is
 *  controlling PTZ via joystick.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_SpaceNotSupported
 *  ONVIF_ERR_InvalidTranslation
 *  ONVIF_ERR_TimeoutNotSupported
 *  ONVIF_ERR_InvalidVelocity
 **/
ONVIF_RET onvif_ptz_ContinuousMove(ptz_ContinuousMove_REQ * p_req)
{
    PTZNodeList * p_node;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    if (p_req->Velocity.PanTiltFlag)
    {
        if (p_req->Velocity.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.XRange.Min < -FPP || 
             p_req->Velocity.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.XRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidVelocity;
        }

        if (p_req->Velocity.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.YRange.Min < -FPP || 
            p_req->Velocity.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.ContinuousPanTiltVelocitySpace.YRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidVelocity;
        }
    }
    
    if (p_req->Velocity.ZoomFlag && 
        (p_req->Velocity.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.ContinuousZoomVelocitySpace.XRange.Min < -FPP || 
         p_req->Velocity.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.ContinuousZoomVelocitySpace.XRange.Max > FPP))
    {
        return ONVIF_ERR_InvalidVelocity;
    }
    
    // todo : here add handler code ...
    
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A PTZ-capable device shall support the Stop operation. If no stop filter 
 *  arguments are present, this command stops all ongoing pan, tilt and zoom
 *  movements. The Stop operation can be filtered to stop a specific movement
 *  by setting the corresponding stop argument.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 **/
ONVIF_RET onvif_ptz_Stop(ptz_Stop_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    // todo : here add handler code ...

    
    return ONVIF_OK;
}

/**
 * @brief
 *  If a PTZ node supports absolute pan/tilt or absolute zoom movements, 
 *  it shall support the AbsoluteMove operation. Theposition argument of
 *  this command specifies the absolute position to which the PTZ unit 
 *  moves. It splits into an optional pan/tilt element and an optional 
 *  zoom element. If the pan/tilt position is omitted, the current 
 *  pan/tilt movement shall not be affected by this command. The same 
 *  holds for the zoom position.
 *
 *  The spaces referenced within the position shall be absolute position 
 *  spaces supported by the PTZ node. If the space information is omitted, 
 *  the corresponding default spaces of the PTZ configuration, a part of 
 *  the specified media profile, is used. A device may support absolute 
 *  pan/tilt movements, absolute zoom movements or no absolute movements by
 *  providing only absolute position spaces for the supported cases.
 *
 *  An existing Speed argument overrides DefaultSpeed of the corresponding 
 *  PTZ configuration during movement to the requested position. If spaces 
 *  are referenced within the Speed argument, they shall be speed spaces 
 *  supported by the PTZ node.
 *
 *  The operation shall fail if the requested absolute position is not reachable.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_SpaceNotSupported
 *  ONVIF_ERR_InvalidPosition
 *  ONVIF_ERR_InvalidSpeed
 **/
ONVIF_RET onvif_ptz_AbsoluteMove(ptz_AbsoluteMove_REQ * p_req)
{
    PTZNodeList * p_node;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    if (p_req->Position.PanTiltFlag)
    {
        if (p_req->Position.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.XRange.Min < -FPP || 
             p_req->Position.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.XRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidPosition;
        }

        if (p_req->Position.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.YRange.Min < -FPP || 
            p_req->Position.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.AbsolutePanTiltPositionSpace.YRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidPosition;
        }
    }

    if (p_req->Position.ZoomFlag && 
        (p_req->Position.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.AbsoluteZoomPositionSpace.XRange.Min < -FPP || 
         p_req->Position.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.AbsoluteZoomPositionSpace.XRange.Max > FPP))
    {
        return ONVIF_ERR_InvalidPosition;
    }    
    
    // todo : here add handler code ...

    
    return ONVIF_OK;
}

/**
 * @brief
 *  If a PTZ node supports relative pan/tilt or relative zoom movements, 
 *  then it shall support the RelativeMove operation. The translation 
 *  argument of this operation specifies the difference from the current
 *  position to the position to which the PTZ device is instructed to move. 
 *  The operation is split into an optional pan/tilt element and an optional
 *  zoom element. If the pan/tilt element is omitted, the current pan/tilt 
 *  movement shall NOT be affected by this command. The same holds for the 
 *  zoom element.
 *
 *  The spaces referenced within the translation element shall be translation
 *  spaces supported by the PTZ node. If the space information is omitted
 *  for the translation argument, the corresponding default spaces of the 
 *  PTZ configuration, which is part of the specified media profile, is used.
 *  A device may support relative pan/tilt movements, relative Zoom movements
 *  or no relative movements by providing only translation spaces for the 
 *  supported cases.
 *
 *  An existing speed argument overrides DefaultSpeed of the corresponding 
 *  PTZ configuration during movement by the requested translation. 
 *  If spaces are referenced within the speed argument, they shall be speed 
 *  spaces supported by the PTZ node.
 *  
 *  The command can be used to stop the PTZ unit at its current position by
 *  sending zero values for pan/tilt and zoom. Stopping shall have the very
 *  same effect independent of the relative space referenced.
 *
 *  If the requested translation leads to an absolute position which cannot
 *  be reached, the PTZ node shall move to a reachable position along the 
 *  border of valid positions.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_SpaceNotSupported
 *  ONVIF_ERR_InvalidTranslation
 *  ONVIF_ERR_InvalidSpeed
 **/
ONVIF_RET onvif_ptz_RelativeMove(ptz_RelativeMove_REQ * p_req)
{
    PTZNodeList * p_node;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    if (p_req->Translation.PanTiltFlag)
    {
        if (p_req->Translation.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.XRange.Min < -FPP || 
            p_req->Translation.PanTilt.x - p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.XRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidTranslation;
        }

        if (p_req->Translation.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.YRange.Min < -FPP || 
            p_req->Translation.PanTilt.y - p_node->PTZNode.SupportedPTZSpaces.RelativePanTiltTranslationSpace.YRange.Max > FPP)
        {
            return ONVIF_ERR_InvalidTranslation;
        }
    }
    
    if (p_req->Translation.ZoomFlag && 
        (p_req->Translation.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.RelativeZoomTranslationSpace.XRange.Min < -FPP || 
         p_req->Translation.Zoom.x - p_node->PTZNode.SupportedPTZSpaces.RelativeZoomTranslationSpace.XRange.Max > FPP))
    {
        return ONVIF_ERR_InvalidTranslation;
    }
    
    // todo : here add handler code ...

    
    return ONVIF_OK;
}

/**
 * @brief
 *  The SetPreset command saves the current device position parameters so 
 *  that the device can move to the saved preset position through the 
 *  GotoPreset operation.
 *
 *  If the PresetToken parameter is absent, the device shall create a new
 *  preset. Otherwise it shall update the stored position and optionally 
 *  the name of the given preset. If creation is successful, the response
 *  contains the PresetToken which uniquely identifies the preset. 
 *  An existing preset can be overwritten by specifying the PresetToken of
 *  the corresponding preset. In both cases (overwriting or creation) an 
 *  optional PresetName can be specified. The operation fails if the PTZ 
 *  device is moving during the SetPreset operation.
 *
 *  The device may internally save additional states such as imaging 
 *  properties in the PTZ preset which then should be recalled
 *  in the GotoPreset operation. A device shall accept a valid 
 *  SetPresetRequest that does not include the optional element
 *  PresetName.
 *
 *  Devices may require unique preset names and reject a request that 
 *  contains an already existing PresetName by responding with the error
 *  message ter:PresetExist.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_PresetExist
 *  ONVIF_ERR_InvalidPresetName
 *  ONVIF_ERR_NoToken
 *  ONVIF_ERR_MovingPTZ
 *  ONVIF_ERR_TooManyPresets
 **/
ONVIF_RET onvif_ptz_SetPreset(ptz_SetPreset_REQ * p_req)
{
    PTZPresetList * p_preset = NULL;
    ONVIF_PROFILE * p_profile;

    p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    if (p_req->PresetTokenFlag && p_req->PresetToken[0] != '\0')
    {
        p_preset = onvif_find_PTZPreset(p_profile->presets, p_req->PresetToken);
        if (NULL == p_preset)
        {
            return ONVIF_ERR_NoToken;
        }
    }
    else
    {
        p_preset = onvif_add_PTZPreset(&p_profile->presets);
        if (NULL == p_preset)
        {
            return ONVIF_ERR_TooManyPresets;
        }
    }

    if (p_req->PresetNameFlag && p_req->PresetName[0] != '\0')
    {
        strcpy(p_preset->PTZPreset.Name, p_req->PresetName);
    }
    else
    {
        strcpy(p_req->PresetName, p_preset->PTZPreset.Name);
    }
    
    if (p_req->PresetTokenFlag && p_req->PresetToken[0] != '\0')
    {
        strcpy(p_preset->PTZPreset.token, p_req->PresetToken);
    }
    else
    {
        strcpy(p_req->PresetToken, p_preset->PTZPreset.token);
    }

    // todo : get PTZ current position ...
    p_preset->PTZPreset.PTZPositionFlag = 1;
    p_preset->PTZPreset.PTZPosition.PanTiltFlag = 1;
    p_preset->PTZPreset.PTZPosition.PanTilt.x = 0;
    p_preset->PTZPreset.PTZPosition.PanTilt.y = 0;
    p_preset->PTZPreset.PTZPosition.ZoomFlag = 1;
    p_preset->PTZPreset.PTZPosition.Zoom.x = 0;
    
    return ONVIF_OK;
}

/**
 * @brief
 *  The RemovePreset operation removes a previously set preset.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_NoToken
 **/
ONVIF_RET onvif_ptz_RemovePreset(ptz_RemovePreset_REQ * p_req)
{
    ONVIF_PROFILE * p_profile;
    PTZPresetList * p_preset;

    p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_preset = onvif_find_PTZPreset(p_profile->presets, p_req->PresetToken);
    if (NULL == p_preset)
    {
        return ONVIF_ERR_NoToken;
    }

    onvif_free_PTZPreset(&p_profile->presets, p_preset);

    return ONVIF_OK;
}

/**
 * @brief
 *  The GotoPreset operation recalls a previously set preset. If the speed 
 *  parameter is omitted, the default speed of the corresponding PTZ 
 *  configuration shall be used. The speed parameter can only be specified
 *  when speed spaces are available for the PTZ node. The GotoPreset command
 *  is a non-blocking operation and can be interrupted by other move commands.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_SpaceNotSupported
 *  ONVIF_ERR_NoToken
 *  ONVIF_ERR_InvalidSpeed
 **/
ONVIF_RET onvif_ptz_GotoPreset(ptz_GotoPreset_REQ * p_req)
{    
    ONVIF_PROFILE * p_profile;
    PTZPresetList * p_preset;
    
    p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_preset = onvif_find_PTZPreset(p_profile->presets, p_req->PresetToken);
    if (NULL == p_preset)
    {
        return ONVIF_ERR_NoToken;
    }

    // todo : here add handler code ...
    

    return ONVIF_OK;
}

/**
 * @brief
 *  This operation moves the PTZ unit to its home position. If the speed 
 *  parameter is omitted, the default speed of the corresponding PTZ 
 *  configuration shall be used. The speed parameter can only be specified 
 *  when speed spaces are available for the PTZ node.The command is 
 *  non-blocking and can be interrupted by other move commands.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_NoHomePosition
 *  ONVIF_ERR_InvalidSpeed
 **/
ONVIF_RET onvif_ptz_GotoHomePosition(ptz_GotoHomePosition_REQ * p_req)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    // todo : here add handler code ...

    return ONVIF_OK;
}

/**
 * @brief
 *  The SetHome operation saves the current position parameters as the home
 *  position, so that the GotoHome operation can request that the device
 *  move to the home position.
 *
 *  The SetHomePosition command shall return with a failure if the "home" 
 *  position is fixed and cannot be overwritten. If the SetHomePosition is
 *  successful, it shall be possible to recall the home position with the
 *  GotoHomePosition command.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_CannotOverwriteHome
 **/
ONVIF_RET onvif_ptz_SetHomePosition(const char * token)
{
    PTZNodeList * p_node;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, token);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    if (p_node->PTZNode.FixedHomePosition)
    {
        return ONVIF_ERR_CannotOverwriteHome;
    }
    
    // todo : here add handler code ...

    return ONVIF_OK;
}

/**
 * @brief
 *  A PTZ-capable device shall implement the SetConfiguration operation. 
 *  The ForcePersistence flag indicates if the changes remain after reboot
 *  of the device.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_ConfigModify
 *  ONVIF_ERR_ConfigurationConflict
 **/
ONVIF_RET onvif_ptz_SetConfiguration(ptz_SetConfiguration_REQ * p_req)
{
    PTZConfigurationList * p_ptz_cfg;
    PTZNodeList * p_ptz_node;

    p_ptz_cfg = onvif_find_PTZConfiguration(g_onvif_cfg.ptz_cfg, p_req->PTZConfiguration.token);
    if (NULL == p_ptz_cfg)
    {
        return ONVIF_ERR_NoConfig;
    }
    
    p_ptz_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_req->PTZConfiguration.NodeToken);
    if (NULL == p_ptz_node)
    {
        return ONVIF_ERR_ConfigModify;
    }

    if (p_req->PTZConfiguration.DefaultPTZTimeoutFlag)
    {
        if (p_req->PTZConfiguration.DefaultPTZTimeout < p_ptz_cfg->Options.PTZTimeout.Min ||
            p_req->PTZConfiguration.DefaultPTZTimeout > p_ptz_cfg->Options.PTZTimeout.Max)
        {
            return ONVIF_ERR_ConfigModify;
        }
    }

    // todo : here add handler code ...


    if (p_req->PTZConfiguration.MoveRampFlag)
    {
        p_ptz_cfg->Configuration.MoveRamp = p_req->PTZConfiguration.MoveRamp;
    }
    
    if (p_req->PTZConfiguration.PresetRampFlag)
    {
        p_ptz_cfg->Configuration.PresetRamp = p_req->PTZConfiguration.PresetRamp;
    }
    
    if (p_req->PTZConfiguration.PresetTourRampFlag)
    {
        p_ptz_cfg->Configuration.PresetTourRamp = p_req->PTZConfiguration.PresetTourRamp;
    }    

    strcpy(p_ptz_cfg->Configuration.Name, p_req->PTZConfiguration.Name);
    
    if (p_req->PTZConfiguration.DefaultPTZSpeedFlag)
    {
        if (p_req->PTZConfiguration.DefaultPTZSpeed.PanTiltFlag)
        {
            p_ptz_cfg->Configuration.DefaultPTZSpeed.PanTilt.x = p_req->PTZConfiguration.DefaultPTZSpeed.PanTilt.x;
            p_ptz_cfg->Configuration.DefaultPTZSpeed.PanTilt.y = p_req->PTZConfiguration.DefaultPTZSpeed.PanTilt.y;
        }

        if (p_req->PTZConfiguration.DefaultPTZSpeed.ZoomFlag)
        {
            p_ptz_cfg->Configuration.DefaultPTZSpeed.Zoom.x = p_req->PTZConfiguration.DefaultPTZSpeed.Zoom.x;
        }
    }

    if (p_req->PTZConfiguration.DefaultPTZTimeoutFlag)
    {
        p_ptz_cfg->Configuration.DefaultPTZTimeout = p_req->PTZConfiguration.DefaultPTZTimeout;
    }

    if (p_req->PTZConfiguration.PanTiltLimitsFlag)
    {
        memcpy(&p_ptz_cfg->Configuration.PanTiltLimits, &p_req->PTZConfiguration.PanTiltLimits, sizeof(onvif_PanTiltLimits));
    }

    if (p_req->PTZConfiguration.ZoomLimitsFlag)
    {
        memcpy(&p_ptz_cfg->Configuration.ZoomLimits, &p_req->PTZConfiguration.ZoomLimits, sizeof(onvif_ZoomLimits));
    }

    if (p_req->PTZConfiguration.ExtensionFlag)
    {
        if (p_req->PTZConfiguration.Extension.PTControlDirectionFlag)
        {
            if (p_req->PTZConfiguration.Extension.PTControlDirection.EFlipFlag)
            {
                p_ptz_cfg->Configuration.Extension.PTControlDirection.EFlip = p_req->PTZConfiguration.Extension.PTControlDirection.EFlip;
            }

            if (p_req->PTZConfiguration.Extension.PTControlDirection.ReverseFlag)
            {
                p_ptz_cfg->Configuration.Extension.PTControlDirection.Reverse = p_req->PTZConfiguration.Extension.PTControlDirection.Reverse;
            }
        }
    }
    
#ifdef MEDIA2_SUPPORT
    onvif_MediaConfigurationChangedNotify(p_req->PTZConfiguration.token, "PTZ");
#endif

    return ONVIF_OK;
}

/**
 * @brief
 *  A device supporting preset tours shall provide options for how preset 
 *  tours can be configured through GetPresetTourOptions.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_NoToken
 **/
ONVIF_RET onvif_ptz_GetPresetTourOptions(ptz_GetPresetTourOptions_REQ * p_req, ptz_GetPresetTourOptions_RES * p_res)
{
    uint32 cnt = 0;
    PTZPresetList * p_preset;
    PresetTourList * p_tour;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    if (p_req->PresetTourTokenFlag)
    {
        p_tour = onvif_find_PresetTour(p_profile->preset_tour, p_req->PresetTourToken);
        if (NULL == p_tour)
        {
            return ONVIF_ERR_NoToken;
        }
    }

    // todo : here add handler code ...
    

    p_res->Options.AutoStart = FALSE;
    p_res->Options.StartingCondition.RecurringTimeFlag = 1;
    p_res->Options.StartingCondition.RecurringTime.Min = 10;
    p_res->Options.StartingCondition.RecurringTime.Max = 100;

    p_res->Options.StartingCondition.RecurringDurationFlag = 1;
    p_res->Options.StartingCondition.RecurringDuration.Min = 10;
    p_res->Options.StartingCondition.RecurringDuration.Max = 100;

    p_res->Options.StartingCondition.PTZPresetTourDirection_Backward = 1;
    p_res->Options.StartingCondition.PTZPresetTourDirection_Forward = 1;

    p_res->Options.TourSpot.PresetDetail.HomeFlag = 1;
    p_res->Options.TourSpot.PresetDetail.Home = TRUE;

    p_preset = p_profile->presets;
    while (p_preset)
    {
        strcpy(p_res->Options.TourSpot.PresetDetail.PresetToken[cnt], p_preset->PTZPreset.token);

        cnt++;
        if (cnt >= ARRAY_SIZE(p_res->Options.TourSpot.PresetDetail.PresetToken))
        {
            break;
        }
        
        p_preset = p_preset->next;
    }

    p_res->Options.TourSpot.PresetDetail.sizePresetToken = cnt;
    
    p_res->Options.TourSpot.StayTime.Min = 0;
    p_res->Options.TourSpot.StayTime.Max = 100;
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A device supporting Preset Tour feature shall allow creating a new 
 *  Preset Tour through the CreatePresetTour.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_TooManyPresetTours
 **/
ONVIF_RET onvif_ptz_CreatePresetTour(ptz_CreatePresetTour_REQ * p_req, ptz_CreatePresetTour_RES * p_res)
{
    int cnt;
    PTZNodeList * p_node;
    PresetTourList * p_tour;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node)
    {
        return ONVIF_ERR_NoPTZProfile;
    }
    
    cnt = onvif_count_PresetTours(p_profile->preset_tour);
    
    if (p_node->PTZNode.ExtensionFlag && 
        p_node->PTZNode.Extension.SupportedPresetTourFlag && 
        cnt >= p_node->PTZNode.Extension.SupportedPresetTour.MaximumNumberOfPresetTours)
    {
        return ONVIF_ERR_TooManyPresetTours;
    }

    p_tour = onvif_add_PresetTour(&p_profile->preset_tour);
    if (p_tour)
    {
        strcpy(p_res->PresetTourToken, p_tour->PresetTour.token);
    }
    else
    {
        return ONVIF_ERR_TooManyPresetTours;
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A device supporting preset tours shall allow modifying a preset tour 
 *  through ModifyPresetTour.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_InvalidPresetTour
 *  ONVIF_ERR_TooManyPresets
 *  ONVIF_ERR_NoToken
 *  ONVIF_ERR_SpaceNotSupported
 **/
ONVIF_RET onvif_ptz_ModifyPresetTour(ptz_ModifyPresetTour_REQ * p_req)
{
    PresetTourList * p_tour;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_tour = onvif_find_PresetTour(p_profile->preset_tour, p_req->PresetTour.token);
    if (NULL == p_tour)
    {
        return ONVIF_ERR_NoToken;
    }

    // todo : here add handler code ...


    strcpy(p_tour->PresetTour.Name, p_req->PresetTour.Name);
    memcpy(&p_tour->PresetTour.StartingCondition, &p_req->PresetTour.StartingCondition, sizeof(onvif_PTZPresetTourStartingCondition));

    onvif_free_PTZPresetTourSpots(&p_tour->PresetTour.TourSpot);

    p_tour->PresetTour.TourSpot = p_req->PresetTour.TourSpot;
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A device supporting preset tours shall allow starting, stopping, or 
 *  pausing a preset tour through OperatePresetTour.
 *
 *  Preset tour can be operated with the PresetTourOperation parameter
 *  of OperatePresetTour command.
 *  Start: indicates starting the preset tour or re-starting the paused preset tour.
 *  Stop: indicates stopping the preset tour.
 *  Pause:iIndicates pausing the preset tour.
 *
 *  When receiving another OperatePresetTour command of Start operation 
 *  for a preset tour which has already been started, the preset tour 
 *  shall be restarted with the newly requested parameter.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_InvalidPresetTour
 *  ONVIF_ERR_NoToken
 *  ONVIF_ERR_ActivationFailed
 **/
ONVIF_RET onvif_ptz_OperatePresetTour(ptz_OperatePresetTour_REQ * p_req)
{
    PresetTourList * p_tour;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_tour = onvif_find_PresetTour(p_profile->preset_tour, p_req->PresetTourToken);
    if (NULL == p_tour)
    {
        return ONVIF_ERR_NoToken;
    }

    // todo : here add handler code ...
    

    if (PTZPresetTourOperation_Start == p_req->Operation)
    {
        p_tour->PresetTour.Status.State = PTZPresetTourState_Touring;
    }
    else if (PTZPresetTourOperation_Pause == p_req->Operation)
    {
        p_tour->PresetTour.Status.State = PTZPresetTourState_Paused;
    }
    else if (PTZPresetTourOperation_Stop == p_req->Operation)
    {
        p_tour->PresetTour.Status.State = PTZPresetTourState_Idle;
    }
    
    return ONVIF_OK;
}

/**
 * @brief
 *  A device supporting preset tours shall support removing preset tours
 *  through RemoevPresetTour.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_NoToken
 **/
ONVIF_RET onvif_ptz_RemovePresetTour(ptz_RemovePresetTour_REQ * p_req)
{
    PresetTourList * p_tour;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_tour = onvif_find_PresetTour(p_profile->preset_tour, p_req->PresetTourToken);
    if (NULL == p_tour)
    {
        return ONVIF_ERR_NoToken;
    }

    // todo : here add handler code ...
    

    onvif_free_PresetTour(&p_profile->preset_tour, p_tour);
    
    return ONVIF_OK;
}

/**
 * @brief
 *  This operation is used to call an auxiliary operation on the device. 
 *  The supported commands can be retrieved via the PTZ node properties.
 *  The auxiliary command should match the supported command listed in the
 *  PTZ node; no other syntax is supported. If the PTZ node lists the 
 *  tt:IRLamp command, then the parameter of AuxiliaryCommand command shall
 *  conform to the syntax specified in Section 8.6 Auxiliary operation of 
 *  ONVIF Core Specification. The SendAuxiliaryCommand shall be implemented
 *  when the PTZ node supports auxiliary commands.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 **/
ONVIF_RET onvif_ptz_SendAuxiliaryCommand(ptz_SendAuxiliaryCommand_REQ * p_req, ptz_SendAuxiliaryCommand_RES * p_res)
{
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    // todo : here add handler code ...

    
    return ONVIF_OK;
}

/**
 * @brief
 *  A device signaling GeoMove in one of its PTZ nodes shall support this command.
 *
 *  The optional AreaHeight and AreaWidth parameters can be added to the 
 *  request, so that the PTZ-capable device can internally determine the 
 *  zoom factor. In case both AreaHeight and AreaWidth are not provided, 
 *  the unit will not change the zoom. AreaHeight and AreaWidth are 
 *  expressed in meters.
 *
 *  An existing speed argument overrides the DefaultSpeed of the corresponding
 *  PTZ configuration during movement by the requested translation. If spaces
 *  are referenced within the speed argument, they shall be speed spaces 
 *  supported by the PTZ node.
 *
 *  If the PTZ-capable device does not support automatic retrieval of the 
 *  geolocation, it shall be configured by using SetGeoLocation before it 
 *  can perform geo-referenced commands. If the client requests a GeoMove 
 *  command before the geolocation of the device is configured, the device 
 *  shall return an error.
 *
 *  Depending on the kinematics of the PTZ-capable device, the requested 
 *  position may not be reachable. In this situation the device shall return
 *  an error, signalling that it cannot perform the requested action due 
 *  to physical limitations.
 *
 * @return
 *  The possible return values:
 *  ONVIF_OK
 *  ONVIF_ERR_NoProfile
 *  ONVIF_ERR_NoPTZProfile
 *  ONVIF_ERR_GeoMoveNotSupported
 *  ONVIF_ERR_UnreachablePosition
 *  ONVIF_ERR_TimeoutNotSupported
 *  ONVIF_ERR_GeoLocationUnknown
 **/
ONVIF_RET onvif_ptz_GeoMove(ptz_GeoMove_REQ * p_req)
{
    PTZNodeList * p_node;
    ONVIF_PROFILE * p_profile = onvif_find_profile(g_onvif_cfg.profiles, p_req->ProfileToken);
    if (NULL == p_profile)
    {
        return ONVIF_ERR_NoProfile;
    }
    else if (NULL == p_profile->ptz_cfg)
    {
        return ONVIF_ERR_NoPTZProfile;
    }

    p_node = onvif_find_PTZNode(g_onvif_cfg.ptz_node, p_profile->ptz_cfg->Configuration.NodeToken);
    if (NULL == p_node || !p_node->PTZNode.GeoMove)
    {
        return ONVIF_ERR_GeoMoveNotSupported;
    }

    // todo : here add handler code ... 
    
    
    return ONVIF_OK;
}

#endif // PTZ_SUPPORT



#include "Alembic.h"
#include "AlembicArchiveStorage.h"
#include "AlembicDefinitions.h"
#include "AlembicVisibilityController.h"
#include "Utility.h"
#include "dummy.h"
#include <ILockedTracks.h>
#include "iparamb2.h"
#include "AlembicNames.h"
#include "resource.h"
#include "AlembicMAXScript.h"

// This function returns a pointer to a class descriptor for our Utility
// This is the function that informs max that our plug-in exists and is 
// available to use
static AlembicVisibilityControllerClassDesc sAlembicVisibilityControllerClassDesc;
ClassDesc2* GetAlembicVisibilityControllerClassDesc()
{
	return &sAlembicVisibilityControllerClassDesc;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Alembic_vis_Ctrl_Param_Blk
///////////////////////////////////////////////////////////////////////////////////////////////////

static ParamBlockDesc2 AlembicVisibilityControllerParams(
	0,
	_T(ALEMBIC_VISIBILITY_CONTROLLER_SCRIPTNAME),
	0,
	GetAlembicVisibilityControllerClassDesc(),
	P_AUTO_CONSTRUCT | P_AUTO_UI,
	0,

	// rollout description 
	IDD_ALEMBIC_VISIBILITY_PARAMS, IDS_ALEMBIC, 0, 0, NULL,

    // params
	AlembicVisibilityController::ID_PATH, _T("path"), TYPE_FILENAME, P_RESET_DEFAULT, IDS_PATH,
	    p_default, "",
	    p_ui,        TYPE_EDITBOX,		IDC_PATH_EDIT,
	 	end,
        
	AlembicVisibilityController::ID_IDENTIFIER, _T("identifier"), TYPE_STRING, P_RESET_DEFAULT, IDS_IDENTIFIER,
	    p_default, "",
	    p_ui,        TYPE_EDITBOX,		IDC_IDENTIFIER_EDIT,
	 	end,

	AlembicVisibilityController::ID_TIME, _T("time"), TYPE_FLOAT, P_ANIMATABLE, IDS_TIME,
		p_default,       0.0f,
		p_range,         0.0f, 1000.0f,
		p_ui,            TYPE_SPINNER,       EDITTYPE_FLOAT, IDC_TIME_EDIT,    IDC_TIME_SPIN, 0.01f,
		end,

	AlembicVisibilityController::ID_MUTED, _T("muted"), TYPE_BOOL, P_ANIMATABLE, IDS_MUTED,
		p_default,       FALSE,
		p_ui,            TYPE_SINGLECHEKBOX,  IDC_MUTED_CHECKBOX,
		end,

	end
);



///////////////////////////////////////////////////////////////////////////////////////////////////
// AlembicVisibilityController Methods
///////////////////////////////////////////////////////////////////////////////////////////////////
IObjParam *AlembicVisibilityController::ip = NULL;
AlembicVisibilityController *AlembicVisibilityController::editMod = NULL;

void AlembicVisibilityController::GetValueLocalTime(TimeValue t, void *ptr, Interval &valid, GetSetMethod method)
{
 	ESS_CPP_EXCEPTION_REPORTING_START

	Interval interval = FOREVER;

    MCHAR const* strPath = NULL;
	this->pblock->GetValue( AlembicVisibilityController::ID_PATH, t, strPath, interval);

	MCHAR const* strIdentifier = NULL;
	this->pblock->GetValue( AlembicVisibilityController::ID_IDENTIFIER, t, strIdentifier, interval);
 
	float fTime;
	this->pblock->GetValue( AlembicVisibilityController::ID_TIME, t, fTime, interval);

	BOOL bMuted;
	this->pblock->GetValue( AlembicVisibilityController::ID_MUTED, t, bMuted, interval);

    if (bMuted)
    {
        return;
    }
	
	Alembic::AbcGeom::IObject iObj = getObjectFromArchive(strPath, strIdentifier);
    
	if(!iObj.valid()) {
        return;
	}

    alembic_fillvis_options visOptions;
    visOptions.pIObj = &iObj;
    visOptions.dTicks = t;
    visOptions.bOldVisibility = m_bOldVisibility;
    AlembicImport_FillInVis(visOptions);

    float fBool = visOptions.bVisibility ? 1.0f : 0.0f;
    m_bOldVisibility = visOptions.bVisibility;

	valid = interval;

	if (method == CTRL_ABSOLUTE)
	{
		float* fInVal = (float*)ptr;
		*fInVal = fBool;
	}
	else // CTRL_RELATIVE
	{
		float* fInVal = (float*)ptr;
		*fInVal = fBool * (*fInVal);
	}

	ESS_CPP_EXCEPTION_REPORTING_END
}

AlembicVisibilityController::AlembicVisibilityController()
{
    pblock = NULL;
    sAlembicVisibilityControllerClassDesc.MakeAutoParamBlocks(this);
    m_bOldVisibility = true;
}

AlembicVisibilityController::~AlembicVisibilityController()
{
    delRefArchive(m_CachedAbcFile);
}

RefTargetHandle AlembicVisibilityController::Clone(RemapDir& remap) 
{
	AlembicVisibilityController *ctrl = new AlembicVisibilityController();
    ctrl->ReplaceReference (0, remap.CloneRef(pblock));
	
    BaseClone(this, ctrl, remap);
	return ctrl;
}

void AlembicVisibilityController::SetValueLocalTime(TimeValue t, void *ptr, int commit, GetSetMethod method)
{
}

void* AlembicVisibilityController::CreateTempValue()
{
    return new float;
}

void AlembicVisibilityController::DeleteTempValue(void *val)
{
    delete (float*)val;
}

void AlembicVisibilityController::ApplyValue(void *val, void *delta)
{
    float &fdelta = *((float*)delta);
    float &fval = *((float*)val);
    fval = fdelta * fval;
}

void AlembicVisibilityController::MultiplyValue(void *val, float m)
{
     float *fVal = (float*)val;
     *fVal = (*fVal) * m;
}

void AlembicVisibilityController::Extrapolate(Interval range, TimeValue t, void *val, Interval &valid, int type)
{
}

#define LOCK_CHUNK		0x2535  //the lock value
IOResult AlembicVisibilityController::Save(ISave *isave)
{
	Control::Save(isave);

	// note: if you add chunks, it must follow the LOCK_CHUNK chunk due to Renoir error in 
	// placement of Control::Save(isave);
	ULONG nb;
	int on = (mLocked==true) ? 1 :0;
	isave->BeginChunk(LOCK_CHUNK);
	isave->Write(&on,sizeof(on),&nb);	
	isave->EndChunk();	

	return IO_OK;
}

IOResult AlembicVisibilityController::Load(ILoad *iload)
{
	ULONG nb;
	IOResult res;

	res = Control::Load(iload);
	if (res!=IO_OK)  return res;

	// We can't do the standard 'while' loop of opening chunks and checking ID
	// since that will eat the Control ORT chunks that were saved improperly in Renoir
	USHORT next = iload->PeekNextChunkID();
	if (next == LOCK_CHUNK) 
	{
		iload->OpenChunk();
		int on;
		res=iload->Read(&on,sizeof(on),&nb);
		if(on)
			mLocked = true;
		else
			mLocked = false;
		iload->CloseChunk();
		if (res!=IO_OK)  return res;
	}

	// Only do anything if this is the control base classes chunk
	next = iload->PeekNextChunkID();
	if (next == CONTROLBASE_CHUNK) 
		res = Control::Load(iload);  // handle improper Renoir Save order
	return res;	
}

RefResult AlembicVisibilityController::NotifyRefChanged(
    Interval iv, 
    RefTargetHandle hTarg, 
    PartID& partID, 
    RefMessage msg) 
{
    switch (msg) 
    {
    case REFMSG_CHANGE:
        if (hTarg == pblock) 
        {
            ParamID changing_param = pblock->LastNotifyParamID();
            switch(changing_param)
            {
            case ID_PATH:
                {
                    delRefArchive(m_CachedAbcFile);
                    MCHAR const* strPath = NULL;
                    TimeValue t = GetCOREInterface()->GetTime();
                    pblock->GetValue( AlembicVisibilityController::ID_PATH, t, strPath, iv);
                    m_CachedAbcFile = strPath;
                    addRefArchive(m_CachedAbcFile);
                }
                break;
            default:
                break;
            }

            AlembicVisibilityControllerParams.InvalidateUI(changing_param);
        }
        break;

    case REFMSG_OBJECT_CACHE_DUMPED:
        return REF_STOP;
        break;
    }

    return REF_SUCCEED;
}

void AlembicVisibilityController::BeginEditParams(IObjParam *ip,ULONG flags,Animatable *prev)
{
	this->ip = ip;
    editMod  = this;

	sAlembicVisibilityControllerClassDesc.BeginEditParams(ip, this, flags, prev);

    // Necessary?
	NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

void AlembicVisibilityController::EndEditParams( IObjParam *ip, ULONG flags, Animatable *next )
{
	sAlembicVisibilityControllerClassDesc.EndEditParams(ip, this, flags, next);

	this->ip = NULL;
    editMod  = NULL;
}



void AlembicImport_FillInVis_Internal(alembic_fillvis_options &options);

void AlembicImport_FillInVis(alembic_fillvis_options &options)
{
	ESS_STRUCTURED_EXCEPTION_REPORTING_START
		AlembicImport_FillInVis_Internal( options );
	ESS_STRUCTURED_EXCEPTION_REPORTING_END
}

void AlembicImport_FillInVis_Internal(alembic_fillvis_options &options)
{
    if(!options.pIObj->valid())
    {
        options.bVisibility = options.bOldVisibility;
        return;
    }

    Alembic::AbcGeom::IVisibilityProperty visibilityProperty = 
        Alembic::AbcGeom::GetVisibilityProperty(*options.pIObj);
    
    if(!visibilityProperty.valid())
    {
        options.bVisibility = options.bOldVisibility;
        return;
    }

    double sampleTime = GetSecondsFromTimeValue(options.dTicks); 
    SampleInfo sampleInfo = getSampleInfo(
        sampleTime,
        getTimeSamplingFromObject(*options.pIObj),
        visibilityProperty.getNumSamples()
        );

    boost::int8_t rawVisibilityValue = visibilityProperty.getValue ( sampleInfo.floorIndex );
    Alembic::AbcGeom::ObjectVisibility visibilityValue = Alembic::AbcGeom::ObjectVisibility ( rawVisibilityValue );

    switch(visibilityValue)
    {
    case Alembic::AbcGeom::kVisibilityVisible:
        {
            options.bVisibility = true;
            break;
        }
    case Alembic::AbcGeom::kVisibilityHidden:
        {
            options.bVisibility = false;
            break;
        }
    default:
        {
            options.bVisibility = options.bOldVisibility;
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// AlembicImport_vis
///////////////////////////////////////////////////////////////////////////////////////////////////
void AlembicImport_SetupVisControl( std::string const& file, std::string const& identifier, Alembic::AbcGeom::IObject &obj, INode *pNode, alembic_importoptions &options )
{
    if (!pNode)
        return;

    if (options.importVisibility == VisImport_JustImportValue)
    {
        alembic_fillvis_options visFillOptions;
        visFillOptions.pIObj = &obj;
        visFillOptions.dTicks = 0;
        visFillOptions.bOldVisibility = true;
        AlembicImport_FillInVis(visFillOptions);
        BOOL bVis = visFillOptions.bVisibility?TRUE:FALSE;
        float fBool = bVis ? 1.0f : 0.0f;
        pNode->SetVisibility(0, fBool);
    }
    else if (options.importVisibility == VisImport_ConnectedControllers)
    {
		Alembic::AbcGeom::IVisibilityProperty visibilityProperty = Alembic::AbcGeom::GetVisibilityProperty(obj);
    
		bool isConstant = true;
		if( visibilityProperty.valid() ) {
			isConstant = visibilityProperty.isConstant();
		}

        // Create the xform modifier
        AlembicVisibilityController *pControl = static_cast<AlembicVisibilityController*>
            (GetCOREInterface()->CreateInstance(CTRL_FLOAT_CLASS_ID, ALEMBIC_VISIBILITY_CONTROLLER_CLASSID));

        // Set the alembic id
        TimeValue t = GET_MAX_INTERFACE()->GetTime();

		TimeValue zero( 0 );

		// Set the alembic id
		pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "path" ), zero, file.c_str() );
		pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "identifier" ), zero, identifier.c_str() );
		pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "time" ), zero, 0.0f );
		pControl->GetParamBlockByID( 0 )->SetValue( GetParamIdByName( pControl, 0, "muted" ), zero, FALSE );

        // Add the modifier to the node
        pNode->SetVisController(pControl);

		if( ! isConstant ) {
			char szControllerName[10000];	
			sprintf_s( szControllerName, 10000, "$'%s'.visibility.controller.time", pNode->GetName() );
			AlembicImport_ConnectTimeControl( szControllerName, options );
		}
    }
}


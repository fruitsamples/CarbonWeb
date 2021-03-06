/*
 *  TPNGButton.cpp
 *  CarbonWeb
 *
 *  Created by Ed Voas on Tue Feb 04 2003.
 *  Copyright (c) 2003 Apple Computer, Inc.. All rights reserved.
 *
 */

#include "TPNGButton.h"

const EventTime kMenuDelay = 0.5;

void
TPNGButton::RegisterClass()
{
	static bool sRegistered;
	
	if ( !sRegistered )
	{
		TView::RegisterSubclass( kTPNGButtonClassID, Construct );
		sRegistered = true;
	}
}

OSStatus
TPNGButton::Create( CFStringRef inBaseName, ControlRef* outControl )
{
	OSStatus			err;
	EventRef			event = CreateInitializationEvent();
	
	RegisterClass();

	SetEventParameter( event, 'Name', typeCFStringRef, sizeof( CFStringRef ), &inBaseName );
	
	err = HIObjectCreate( kTPNGButtonClassID, event, (HIObjectRef*)outControl );
	
	ReleaseEvent( event );

	return err;
}

TPNGButton*
TPNGButton::GetFromHIViewRef( HIViewRef inViewRef )
{
	return (TPNGButton*)HIObjectDynamicCast( (HIObjectRef)inViewRef, kTPNGButtonClassID );
}


TPNGButton::TPNGButton( ControlRef inControl ) : TView( inControl )
{
	fNormalImage = fDisabledImage = fPressedImage = NULL;
	fMenu = NULL;
}

TPNGButton::~TPNGButton()
{
	ReleaseImages();
	
	if ( fMenu )
		ReleaseMenu( fMenu );
}

OSStatus
TPNGButton::Construct( HIObjectRef inControl, TObject** outView )
{
	*outView = new TPNGButton( (ControlRef)inControl );
	
	return noErr;
}

UInt32
TPNGButton::GetBehaviors()
{
	return TView::GetBehaviors();
}

ControlKind
TPNGButton::GetKind()
{
	const ControlKind kMyKind = { 'voas', 'TPNG' };
	
	return kMyKind;
}

OSStatus
TPNGButton::Initialize( TCarbonEvent& inEvent )
{
	CFStringRef	name = NULL;
	
	inEvent.GetParameter<CFStringRef>( 'Name', typeCFStringRef, &name );
	
	OSStatus	err = TView::Initialize( inEvent );

	if ( name )
		LoadImages( name );
	
	ChangeAutoInvalidateFlags( kAutoInvalidateOnActivate |
								kAutoInvalidateOnHilite |
								kAutoInvalidateOnEnable, 0 );
	return err;
}

void
TPNGButton::SetImageName( CFStringRef inName )
{
	LoadImages( inName );
	Invalidate();
}

void
TPNGButton::SetMenu( MenuRef inMenu )
{
	if ( inMenu != fMenu )
	{
		if ( fMenu )
			ReleaseMenu( fMenu );
		
		if ( inMenu )
			RetainMenu( inMenu );
		
		fMenu = inMenu;
	}
}

void
TPNGButton::Draw( RgnHandle limitRgn, CGContextRef inContext )
{
	HIRect 		bounds = Bounds();
	CGImageRef	image = NULL;
	bool		createdContext = false;
	GrafPtr		port;
	
	// Here's some fun code to allow us to work in non-composited
	// mode. Oh what fun!

	if ( !IsComposited() && (inContext == NULL) )
	{
		Rect	portRect;
		
		GetPort( &port );
		GetPortBounds( port, &portRect );
		QDBeginCGContext( port, &inContext );
        SyncCGContextOriginWithPort( inContext, port );
		CGContextTranslateCTM( inContext, 0, (portRect.bottom - portRect.top) );
		CGContextScaleCTM( inContext, 1, -1 );
        createdContext = true;
		CGContextSetRGBFillColor( inContext, 1, 1, 1, 1 );
		CGContextFillRect( inContext, bounds );
	}
	
	if ( !IsActive() || !IsEnabled() )
		image = fDisabledImage;
	else if ( GetControlHilite( GetViewRef() ) == 1 )
		image = fPressedImage;
	else
		image = fNormalImage;
	
	if ( image )
		HIViewDrawCGImage( inContext, &bounds, image );
	else
		CGContextStrokeRect( inContext, bounds );
	
	if ( createdContext )
	{
        CGContextSynchronize( inContext );
        QDEndCGContext( port, &inContext );
	}
}

ControlPartCode
TPNGButton::HitTest( const HIPoint& where )
{
	if ( CGRectContainsPoint( Bounds(), where ) )
		return 1;
	else
		return kControlNoPart;
}

OSStatus
TPNGButton::Track(
	TCarbonEvent&		inEvent,
	ControlPartCode*	outPartHit )
{
	OSStatus						err = eventNotHandledErr;
	HIPoint							startPt;
	UInt32							modifiers;
	SInt16							partHit;
	Boolean							wasInside;
	Boolean							inside;
	EventTime						menuOpenTime;
	EventTime						timeout;
	Point							qdPt;
	MouseTrackingResult				mouseResult;
	Boolean							isDisabled;

	// only do our tracking if we have a menu or if we're sticky

	require_quiet( fMenu != NULL, UseDefaultTracking );
	
	err = GetEventParameter( inEvent, kEventParamMouseLocation, typeHIPoint, NULL,
		sizeof( startPt ), NULL, &startPt );
	require_noerr( err, CantGetParameter );

	// start with a default value for the output parameter
	
	*outPartHit = kControlNoPart;
	
	// Get the initial part. If we weren't really hit, let the default thing happen.

	err = HIViewGetPartHit( GetViewRef(), &startPt, &partHit );
	require_noerr( err, CantGetInitialPart );
	require_action_quiet( partHit != kControlNoPart, NotReallyHit, err = eventNotHandledErr );
	
	// draw in the pressed state
	HiliteControl( GetViewRef(), partHit );

	wasInside = inside = true;

	menuOpenTime = GetCurrentEventTime() + kMenuDelay;
		
	while ( true )
	{
		// find out if we are inside the button

		err = HIViewGetPartHit( GetViewRef(), &startPt, &partHit );
		inside = (err == noErr && partHit != kControlNoPart);
		
		// if we moved into or out of the control, do some work
		
		if ( inside != wasInside )
		{
			// recalculate our menu open time now that they have reentered the button

			if ( inside )
				menuOpenTime = GetCurrentEventTime() + kMenuDelay;

			HiliteControl( GetViewRef(), partHit );

			wasInside = inside;
		}
		
		// if we have a menu, display it after waiting the appropriate
		// amount of time (which may be zero).

		if ( inside && GetCurrentEventTime() >= menuOpenTime )
		{
			partHit = TrackMenu();
			break;
		}
		
		// block until the mouse moves, goes up, or our timeout is reached

		timeout = menuOpenTime - GetCurrentEventTime();
		
		err = TrackMouseLocationWithOptions( (GrafPtr)-1L, 0, timeout, &qdPt, &modifiers, &mouseResult );
		
		// Need to convert from global to view-relative.

		startPt = GlobalToViewPoint( qdPt );
		
		// bail out when the mouse is released
		if ( mouseResult == kMouseTrackingMouseReleased )
			break;
	}
	
	// WORKAROUND: The control manager has a little bit of a problem with
	// hilites vs. enabled state. If we have a disabled button (which we might
	// have gotten set as when a menu command was handled), and we call
	// HiliteControl( 0 ), we will enable the button! And I can't just test for
	// being disabled and not call HiliteControl because if I later call EnableControl,
	// we'll draw pressed! Ick. So we need to get our disabled state, save it off, 
	// call HiliteControl, and then disable ourselves again if we were to begin with.
	// Fortunately we only draw once since we're composited.

	isDisabled = !IsControlEnabled( GetViewRef() );

	if ( isDisabled )
		EnableControl( GetViewRef() );

	HiliteControl( GetViewRef(), kControlNoPart );
	
	if ( isDisabled )
		DisableControl( GetViewRef() );
	
	*outPartHit = partHit;

NotReallyHit:
CantGetInitialPart:
CantGetParameter:
UseDefaultTracking:

	return err;
}

ControlPartCode
TPNGButton::TrackMenu()
{
	HIRect			bounds;
	HIPoint			where;
	HIPoint			globalWhere;
	UInt32			result;
	
	bounds = Bounds();
	
	where.x = CGRectGetMinX( bounds );
	where.y = CGRectGetMaxY( bounds );
	
	globalWhere = ViewToGlobalPoint( where );
	
	result = PopUpMenuSelect( fMenu, (SInt16)globalWhere.y, (SInt16)globalWhere.x, 1 );
	
	return result ? kControlMenuPart : kControlNoPart;
}

void
TPNGButton::LoadImages( CFStringRef inName )
{
	CFStringRef		name;
		
	ReleaseImages();

	name = CFStringCreateWithFormat( NULL, NULL, CFSTR( "%@.png" ), inName );
	fNormalImage = LoadImage( name );
	CFRelease( name );
	
	name = CFStringCreateWithFormat( NULL, NULL, CFSTR( "%@Disabled.png" ), inName );
	fDisabledImage = LoadImage( name );
	CFRelease( name );
	
	name = CFStringCreateWithFormat( NULL, NULL, CFSTR( "%@Pressed.png" ), inName );
	fPressedImage = LoadImage( name );
	CFRelease( name );
}

void
TPNGButton::ReleaseImages()
{
	if ( fNormalImage )
	{
		CGImageRelease( fNormalImage );
		fNormalImage = NULL;
	}
	
	if ( fDisabledImage )
	{
		CGImageRelease( fDisabledImage );
		fDisabledImage = NULL;
	}
	
	if ( fPressedImage )
	{
		CGImageRelease( fPressedImage );
		fPressedImage = NULL;
	}	
}

CGImageRef
TPNGButton::LoadImage( CFStringRef inName )
{
	CGDataProviderRef	provider;
	CFBundleRef 		appBundle = ::CFBundleGetMainBundle();
	CGImageRef			image = NULL;
	
	if ( appBundle )
	{
		CFURLRef url = ::CFBundleCopyResourceURL( appBundle, inName, NULL, NULL );
		
		provider = CGDataProviderCreateWithURL( url );

		image = CGImageCreateWithPNGDataProvider( provider, NULL, false,  kCGRenderingIntentDefault );
		
		CGDataProviderRelease( provider );
		CFRelease( url );
	}
	
	return image;
}

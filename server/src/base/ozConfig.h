//
// ZoneMinder Configuration, $Date: 2011-01-22 02:01:05 +0000 (Sat, 22 Jan 2011) $, $Revision: 3249 $
// Copyright (C) 2001-2008 Philip Coombes
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
// 

#include "config.h"
#include "ozConfigDefines.h"

#include <string>

#define OZ_CONFIG				"/opt//etc/oz.conf"	// Path to config file
#define OZ_VERSION				"2.0.0a"	// ZoneMinder Version

#define OZ_MAX_IMAGE_WIDTH		2048				// The largest image we imagine ever handling
#define OZ_MAX_IMAGE_HEIGHT		1536				// The largest image we imagine ever handling
#define OZ_MAX_IMAGE_COLOURS	3					// The largest image we imagine ever handling
#define OZ_MAX_IMAGE_DIM		(OZ_MAX_IMAGE_WIDTH*OZ_MAX_IMAGE_HEIGHT)
#define OZ_MAX_IMAGE_SIZE		(OZ_MAX_IMAGE_DIM*OZ_MAX_IMAGE_COLOURS)

#define OZ_SCALE_BASE			100					// The factor by which we bump up 'scale' to simulate FP
#define OZ_RATE_BASE			100					// The factor by which we bump up 'rate' to simulate FP

#define OZ_SQL_SML_BUFSIZ       256                 // Size of SQL buffer
#define OZ_SQL_MED_BUFSIZ       1024                // Size of SQL buffer
#define OZ_SQL_LGE_BUFSIZ       8192                // Size of SQL buffer

#define OZ_NETWORK_BUFSIZ       32768               // Size of network buffer

#define OZ_MAX_FPS              30                  // The maximum frame rate we expect to handle
#define OZ_SAMPLE_RATE          int(1000000/OZ_MAX_FPS) // A general nyquist sample frequency for delays etc
#define OZ_SUSPENDED_RATE       int(1000000/4) // A slower rate for when disabled etc

extern void ozLoadConfig();

struct StaticConfig
{
    std::string DB_HOST;
    std::string DB_NAME;
    std::string DB_USER;
    std::string DB_PASS;
    std::string PATH_WEB;
};

extern StaticConfig staticConfig;

class ConfigItem
{
private:
	char *name;
	char *value;
	char *type;

	mutable enum { CFG_BOOLEAN, CFG_INTEGER, CFG_DECIMAL, CFG_STRING } cfg_type;
	mutable union
	{
		bool boolean_value;
		int integer_value;
		double decimal_value;
		char *string_value;
	} cfg_value;
	mutable bool accessed;

public:
	ConfigItem( const char *p_name, const char *p_value, const char *const p_type );
	~ConfigItem();
	void ConvertValue() const;
	bool BooleanValue() const;
	int IntegerValue() const;
	double DecimalValue() const;
	const char *StringValue() const;

	inline operator bool() const
	{
		return( BooleanValue() );
	}
	inline operator int() const
	{
		return( IntegerValue() );
	}
	inline operator double() const
	{
		return( DecimalValue() );
	}
	inline operator const char *() const
	{
		return( StringValue() );
	}
};

class Config
{
public:
	OZ_CFG_DECLARE_LIST

private:
	int n_items;
	ConfigItem **items;

public:
	Config();
	~Config();

	void Load();
	void Assign();
	const ConfigItem &Item( int id );
};

extern Config config;

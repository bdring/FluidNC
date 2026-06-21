'''
    Visual studio project file generator
	@authors: atlaste [github.com/atlaste]
'''

PATHS_TO_SEARCH = ['FluidNC']
HEADER_EXT = ['.h', '.inl']
SOURCE_EXT = ['.c', '.cpp']
OTHER_EXT = ['.ino', '.md', '.txt']
TEST_IGNORE = ['I2SOut.cpp','I2SOut.h']

import os, uuid

def UUID(name):
	return str(uuid.uuid3(uuid.NAMESPACE_OID, name)).upper()

def FilterFromPath(path):
	(head, tail) = os.path.split(path)
	head = head.replace('/', '\\').replace('..\\', '').replace('.\\', '')
	if head == '.':
		return ''

	h = head[0:7];
	if h == 'FluidNC':
		h = head[8:]
	return h

class Vcxproj:
	# configuration, platform
	ConfigurationFmt = '\n'.join([
		'	<ProjectConfiguration Include="{0}|{1}">',
		'	  <Configuration>{0}</Configuration>',
		'	  <Platform>{1}</Platform>',
		'	</ProjectConfiguration>'])
	# item name, item file
	ItemFmt = '\n'.join([
		'	<{0} Include="{1}" />'])

	# configuration, platform
	ConfigTypePropertyGroupFmt = '\n'.join([
		'	<PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'{0}|{1}\'" Label="Configuration">',
		'		<ConfigurationType>Makefile</ConfigurationType>',
		'		<UseDebugLibraries>true</UseDebugLibraries>',
		'		<PlatformToolset>v142</PlatformToolset>',
		'	</PropertyGroup>'])

	# configuration, platform
	ImportGroupFmt = '\n'.join([
		'	<ImportGroup Label="PropertySheets" Condition="\'$(Configuration)|$(Platform)\'==\'{0}|{1}\'">',
		'		<Import Project="$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props" Condition="exists(\'$(UserRootDir)\\Microsoft.Cpp.$(Platform).user.props\')" Label="LocalAppDataPlatform" />',
		'	</ImportGroup>'
	])

	# configuration, platform
	PIOPropertyGroupFmt = '\n'.join([
		'	<PropertyGroup Condition="\'$(Configuration)|$(Platform)\'==\'{0}|{1}\'">',
		'		<NMakeBuildCommandLine>platformio --force run</NMakeBuildCommandLine>',
		'		<NMakeCleanCommandLine>platformio --force run -t clean</NMakeCleanCommandLine>',
		'		<NMakePreprocessorDefinitions>WIN32;_DEBUG;$(NMakePreprocessorDefinitions)</NMakePreprocessorDefinitions>',
		'		<NMakeIncludeSearchPath>$(HOMEDRIVE)$(HOMEPATH)\\.platformio\\packages\\framework-arduinoespressif32\\tools\\sdk\\include\\freertos;$(HOMEDRIVE)$(HOMEPATH)\\.platformio\\packages\\toolchain-xtensa32\\xtensa-esp32-elf\\include;$(HOMEDRIVE)$(HOMEPATH)\\.platformio\\packages\\framework-arduinoespressif32\\cores\\esp32;$(HOMEDRIVE)$(HOMEPATH)\\.platformio\\packages\\framework-arduinoespressif32\\tools\\sdk\\include\\driver;$(NMakeIncludeSearchPath)</NMakeIncludeSearchPath>',
		'	</PropertyGroup>'
	])

	@staticmethod
	def Configuration(configuration, platform):
		return Vcxproj.ConfigurationFmt.format(configuration, platform)

	@staticmethod
	def Item(name, file):
		return Vcxproj.ItemFmt.format(name, file)

	@staticmethod
	def ConfigTypePropertyGroup(configuration, platform):
		return Vcxproj.ConfigTypePropertyGroupFmt.format(configuration, platform)

	@staticmethod
	def ImportGroup(configuration, platform):
		return Vcxproj.ImportGroupFmt.format(configuration, platform)

	@staticmethod
	def PIOPropertyGroup(configuration, platform):
		return Vcxproj.PIOPropertyGroupFmt.format(configuration, platform)

class Filters:
	# itemtype, path, folder
	ItemFmt = '\n'.join([
		'	<{0} Include="{1}">',
		'	  <Filter>{2}</Filter>',
		'	</{0}>'])
	
	# folder, uuid
	FilterFmt = '\n'.join([
		'	<Filter Include="{0}">',
		'	  <UniqueIdentifier>{{{1}}}</UniqueIdentifier>',
		'	</Filter>'])

	@staticmethod
	def Item(itemtype, path):
		folder = FilterFromPath(path)
		return Filters.ItemFmt.format(itemtype, path, folder)
	
	@staticmethod
	def Filter(folder):
		uid = UUID(folder)
		return Filters.FilterFmt.format(folder, uid)

class Generator:
	Folders = set()
	
	# Files
	Headers = set()
	Sources = set()
	Others = set()

	# Stuffs
	Platforms = set(['Win32','x64'])
	Configurations = set(['Debug','Release'])
	Name = 'FluidNC'

	def AddFolder(self, path):
		filt = FilterFromPath(path)
		if filt == '':
			return
		if filt not in self.Folders:
			self.Folders.add(filt)
			filters = ''
			for f in os.path.split(filt):
				filters = os.path.join(filters, f)
				if filters != '':
					self.Folders.add(filters)

	def AddFile(self, path):	
		if path.find('/test/') >= 0:
			return
		elif path.find('\\test\\') >= 0:
			return
		elif path.find('/managed_components/') >= 0:
			return
		elif path.find('\\managed_components\\') >= 0:
			return
            
		(root, ext) = os.path.splitext(path)
		if ext in HEADER_EXT:
			self.Headers.add(path)
		elif ext in SOURCE_EXT:
			self.Sources.add(path)
		elif ext in OTHER_EXT:
			self.Others.add(path)
		else:
			return
		
		self.AddFolder(path)

	def Walk(self, path):
		if path == 'FluidNC\\Custom' or path == 'FluidNC/Custom':
			return
		if os.path.isfile(path):
			self.AddFile(path)
		else:
			for subPath in os.listdir(path):
				self.Walk(os.path.join(path, subPath))

	def CreateProject(self):
		project = []
		project.append('<?xml version="1.0" encoding="utf-8"?>')
		project.append('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')

		project.append('<ItemGroup Label="ProjectConfigurations">')
		for p in self.Platforms:
			for c in self.Configurations:
				project.append(Vcxproj.Configuration(c, p))
		project.append('</ItemGroup>')

		# cpp header files
		project.append('<ItemGroup>')
		for f in self.Headers:
			project.append(Vcxproj.Item('ClInclude', f))
		project.append('</ItemGroup>')

		# cpp source files
		project.append('<ItemGroup>')
		for f in self.Sources:
			project.append(Vcxproj.Item('ClCompile', f))
		project.append('</ItemGroup>')

		# md files and ino file
		project.append('<ItemGroup>')
		for f in self.Others:
			project.append(Vcxproj.Item('None', f))
		project.append('</ItemGroup>')

		# Bookkeeping, compilation, etc.
		project.append('<PropertyGroup Label="Globals">')
		project.append('	<VCProjectVersion>16.0</VCProjectVersion>')
		project.append('	<ProjectGuid>{11C8A44F-A303-4885-B5AD-5B65F7FE41C0}</ProjectGuid>')
		project.append('	<Keyword>Win32Proj</Keyword>')
		project.append('</PropertyGroup>')

		project.append('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />')
		for p in self.Platforms:
			for c in self.Configurations:
				project.append(Vcxproj.ConfigTypePropertyGroup(c, p))

		project.append('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />')
		project.append('<ImportGroup Label="ExtensionSettings">')
		project.append('</ImportGroup>')
		project.append('	<ImportGroup Label="Shared">')
		project.append('</ImportGroup>')

		for p in self.Platforms:
			for c in self.Configurations:
				project.append(Vcxproj.ImportGroup(c, p))
		project.append('<PropertyGroup Label="UserMacros" />')
		
		for p in self.Platforms:
			for c in self.Configurations:
				project.append(Vcxproj.PIOPropertyGroup(c, p))
		
		project.append('<ItemDefinitionGroup>')
		project.append('</ItemDefinitionGroup>')
		project.append('<Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />')
		project.append('	<ImportGroup Label="ExtensionTargets">')
		project.append('</ImportGroup>')
		project.append('<ProjectExtensions>')
		project.append('	<VisualStudio>')
		project.append('		<UserProperties config.Debug.customdebug_esp32_esp32_debugger_type="universal" />')
		project.append('	</VisualStudio>')
		project.append('	</ProjectExtensions>')
		project.append('</Project>')
		return '\n'.join(project)

	def CreateFilters(self):
		project = []
		project.append('<?xml version="1.0" encoding="utf-8"?>')
		project.append('<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">')

		project.append('<ItemGroup>')
		for f in self.Folders:
			project.append(Filters.Filter(f))
		project.append('</ItemGroup>')

		project.append('<ItemGroup>')
		for f in self.Headers:
			project.append(Filters.Item('ClInclude', f))
		project.append('</ItemGroup>')

		project.append('<ItemGroup>')
		for f in self.Sources:
			project.append(Filters.Item('ClCompile', f))
		project.append('</ItemGroup>')

		project.append('<ItemGroup>')
		for f in self.Others:
			project.append(Filters.Item('None', f))
		project.append('</ItemGroup>')

		project.append('</Project>')
		return '\n'.join(project)

	def Generate(self):
		f = open(self.Name + '.vcxproj', 'w')
		f.write(self.CreateProject())
		f.close()

		f = open(self.Name + '.vcxproj.filters', 'w')
		f.write(self.CreateFilters())
		f.close()

def main(paths):
	generator = Generator()
	for path in paths:
		generator.Walk(path)
	generator.Generate()
    
def tests(paths):
	generator = Generator()
	generator.Name = "UnitTests"
	OTHER_EXT = ['.md']
	newpaths = ['FluidNC', 'X86TestSupport']
	for path in newpaths:
		generator.Walk(path)
	generator.Generate()

main(PATHS_TO_SEARCH)


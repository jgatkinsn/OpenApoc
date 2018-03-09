#include "framework/serialization/serialize.h"
#include "dependencies/pugixml/src/pugixml.hpp"
#include "dependencies/rapidjson/include/rapidjson/rapidjson.h"
#include "dependencies/rapidjson/include/rapidjson/writer.h"
#include "dependencies/rapidjson/include/rapidjson/prettywriter.h"
#include "dependencies/rapidjson/include/rapidjson/stringbuffer.h"
#include "framework/filesystem.h"
#include "framework/logger.h"
#include "framework/serialization/providers/filedataprovider.h"
#include "framework/serialization/providers/providerwithchecksum.h"
#include "framework/serialization/providers/zipdataprovider.h"
#include "framework/trace.h"
#include "library/sp.h"
#include "library/strings.h"
#include "library/strings_format.h"
#include <map>
#include <sstream>

namespace OpenApoc
{

up<SerializationDataProvider> getProvider(bool pack)
{
	if (!pack)
	{
		// directory loader
		return mkup<ProviderWithChecksum>(mkup<FileDataProvider>());
	}
	else
	{
		// zip loader
		return mkup<ProviderWithChecksum>(mkup<ZipDataProvider>());
	}
}


SerializationNode *SerializationNode::getNodeReq(const UString &name)
{
	auto node = this->getNodeOpt(name);
	if (!node)
	{
		throw SerializationException("Missing node \"" + name + "\"", this);
	}
	return node;
}

SerializationNode *SerializationNode::getSectionReq(const UString &name)
{
	auto node = this->getSectionOpt(name);
	if (!node)
	{
		throw SerializationException("Missing section \"" + name + "\"", this);
	}
	return node;
}

SerializationNode *SerializationNode::getNextSiblingReq(const UString &name)
{
	auto node = this->getNextSiblingOpt(name);
	if (!node)
	{
		throw SerializationException("Missing sibling of \"" + name + "\"", this);
	}
	return node;
}

using namespace pugi;
class XMLSerializationNode;

class XMLSerializationArchive : public SerializationArchive
{
  private:
	up<SerializationDataProvider> dataProvider;
	std::map<UString, xml_document> docRoots;
	std::vector<up<SerializationNode>> nodes;
	friend class SerializationArchive;
	friend class XMLSerializationNode;

  public:
	SerializationNode *newRoot(const UString &prefix, const UString &name) override;
	SerializationNode *getRoot(const UString &prefix, const UString &name) override;
	bool write(const UString &path, bool pack, bool pretty) override;
	XMLSerializationArchive() : dataProvider(nullptr), docRoots(){};
	XMLSerializationArchive(up<SerializationDataProvider> dataProvider)
	    : dataProvider(std::move(dataProvider)){};
	~XMLSerializationArchive() override = default;
};

class XMLSerializationNode : public SerializationNode
{
  private:
	SerializationDataProvider *dataProvider;
	XMLSerializationArchive *archive;
	xml_node node;
	XMLSerializationNode *parent;
	UString prefix;

  public:
	XMLSerializationNode(XMLSerializationArchive *archive, xml_node node,
	                     XMLSerializationNode *parent)
	    : archive(archive), node(node), parent(parent)
	{
	}

	XMLSerializationNode(XMLSerializationArchive *archive, xml_node node, const UString &prefix)
	    : archive(archive), node(node), prefix(prefix), parent(nullptr)
	{
	}

	SerializationNode *addNode(const UString &name, const UString &value = "") override;
	SerializationNode *addSection(const UString &name) override;

	SerializationNode *getNodeOpt(const UString &name) override;
	SerializationNode *getNextSiblingOpt(const UString &name) override;
	SerializationNode *getSectionOpt(const UString &name) override;

	UString getName() override;
	void setName(const UString &str) override;
	UString getValue() override;
	void setValue(const UString &str) override;

	unsigned int getValueUInt() override;
	void setValueUInt(unsigned int i) override;

	unsigned char getValueUChar() override;
	void setValueUChar(unsigned char i) override;

	int getValueInt() override;
	void setValueInt(int i) override;

	unsigned long long getValueUInt64() override;
	void setValueUInt64(unsigned long long i) override;

	long long getValueInt64() override;
	void setValueInt64(long long i) override;

	float getValueFloat() override;
	void setValueFloat(float f) override;

	bool getValueBool() override;
	void setValueBool(bool b) override;

	std::vector<bool> getValueBoolVector() override;
	void setValueBoolVector(const std::vector<bool> &vec) override;

	UString getFullPath() override;
	const UString &getPrefix() const override
	{
		if (this->parent)
			return this->parent->getPrefix();
		else
			return this->prefix;
	}

	~XMLSerializationNode() override = default;
};

SerializationNode *XMLSerializationArchive::newRoot(const UString &prefix, const UString &name)
{
	auto path = prefix + name + ".xml";
	auto root = this->docRoots[path].root().append_child();
	auto decl = this->docRoots[path].prepend_child(pugi::node_declaration);
	decl.append_attribute("version") = "1.0";
	decl.append_attribute("encoding") = "UTF-8";
	root.set_name(name.cStr());
	this->nodes.push_back(mkup<XMLSerializationNode>(this, root, prefix + name + "/"));
	return this->nodes.back().get();
}

SerializationNode *XMLSerializationArchive::getRoot(const UString &prefix, const UString &name)
{
	auto path = prefix + name + ".xml";
	if (dataProvider == nullptr)
	{
		LogWarning("Reading from not opened archive: %s!", path);
		return nullptr;
	}

	auto it = this->docRoots.find(path);
	if (it == this->docRoots.end())
	{
		TraceObj trace("Reading archive", {{"path", path}});
		UString content;
		if (dataProvider->readDocument(path, content))
		{
			// FIXME: Make this actually read from the root and load the xinclude tags properly?
			auto &doc = this->docRoots[path];
			TraceObj traceParse("Parsing archive", {{"path", path}});
			auto parse_result = doc.load_string(content.cStr());
			if (!parse_result)
			{
				LogInfo("Failed to parse \"%s\" : \"%s\" at \"%llu\"", path,
				        parse_result.description(), (unsigned long long)parse_result.offset);
				return nullptr;
			}
			it = this->docRoots.find(path);
			LogInfo("Parsed \"%s\"", path);
		}
	}
	if (it == this->docRoots.end())
	{
		return nullptr;
	}

	auto root = it->second.child(name.cStr());
	if (!root)
	{
		LogWarning("Failed to find root with name \"%s\" in \"%s\"", name, path);
		return nullptr;
	}
	this->nodes.push_back(mkup<XMLSerializationNode>(this, root, prefix + name + "/"));
	return this->nodes.back().get();
}

bool XMLSerializationArchive::write(const UString &path, bool pack, bool pretty)
{
	TraceObj trace("Writing archive", {{"path", path}});
	// warning! data provider must be freed when this method ends,
	// so code calling this method may override archive
	auto dataProvider = getProvider(pack);
	if (!dataProvider->openArchive(path, true))
	{
		LogWarning("Failed to open archive at \"%s\"", path);
		return false;
	}

	for (auto &root : this->docRoots)
	{
		TraceObj traceSave("Saving root", {{"root", root.first}});
		std::stringstream ss;
		unsigned int flags = pugi::format_default;
		if (pretty == false)
		{
			flags = pugi::format_raw;
		}
		root.second.save(ss, "", flags);
		TraceObj traceSaveData("Saving root data", {{"root", root.first}});
		if (!dataProvider->saveDocument(root.first, ss.str()))
		{
			return false;
		}
	}

	return dataProvider->finalizeSave();
}

SerializationNode *XMLSerializationNode::addNode(const UString &name, const UString &value)
{
	auto newNode = this->node.append_child();
	newNode.set_name(name.cStr());
	newNode.text().set(value.cStr());
	this->archive->nodes.push_back(mkup<XMLSerializationNode>(this->archive, newNode, this));
	return this->archive->nodes.back().get();
}

SerializationNode *XMLSerializationNode::getNodeOpt(const UString &name)
{
	auto newNode = this->node.child(name.cStr());
	if (!newNode)
	{
		return nullptr;
	}
	this->archive->nodes.push_back(mkup<XMLSerializationNode>(this->archive, newNode, this));
	return this->archive->nodes.back().get();
}

SerializationNode *XMLSerializationNode::getNextSiblingOpt(const UString &name)
{
	auto newNode = this->node.next_sibling(name.cStr());
	if (!newNode)
	{
		return nullptr;
	}
	this->archive->nodes.push_back(mkup<XMLSerializationNode>(this->archive, newNode, this));
	return this->archive->nodes.back().get();
}

SerializationNode *XMLSerializationNode::addSection(const UString &name)
{
	auto includeNode = static_cast<XMLSerializationNode *>(this->addNode(UString{"xi:include"}));
	auto path = name + ".xml";
	auto nsAttribute = includeNode->node.append_attribute("xmlns:xi");
	nsAttribute.set_value("http://www.w3.org/2001/XInclude");
	auto attribute = includeNode->node.append_attribute("href");
	attribute.set_value(path.cStr());
	return this->archive->newRoot(this->getPrefix(), name);
}

SerializationNode *XMLSerializationNode::getSectionOpt(const UString &name)
{
	return archive->getRoot(this->getPrefix(), name);
}

UString XMLSerializationNode::getName() { return node.name(); }

void XMLSerializationNode::setName(const UString &str) { node.set_name(str.cStr()); }

UString XMLSerializationNode::getValue() { return node.text().get(); }

void XMLSerializationNode::setValue(const UString &str) { node.text().set(str.cStr()); }

unsigned int XMLSerializationNode::getValueUInt() { return node.text().as_uint(); }

void XMLSerializationNode::setValueUInt(unsigned int i) { node.text().set(i); }

unsigned char XMLSerializationNode::getValueUChar()
{
	auto uint = node.text().as_uint();
	if (uint > std::numeric_limits<unsigned char>::max())
	{
		throw SerializationException(format("Value %u is out of range of unsigned char type", uint),
		                             this);
	}
	return static_cast<unsigned char>(uint);
}

void XMLSerializationNode::setValueUChar(unsigned char c) { node.text().set((unsigned int)c); }

int XMLSerializationNode::getValueInt() { return node.text().as_int(); }

void XMLSerializationNode::setValueInt(int i) { node.text().set(i); }

unsigned long long XMLSerializationNode::getValueUInt64() { return node.text().as_ullong(); }

void XMLSerializationNode::setValueUInt64(unsigned long long i) { node.text().set(i); }

long long XMLSerializationNode::getValueInt64() { return node.text().as_llong(); }

void XMLSerializationNode::setValueInt64(long long i) { node.text().set(i); }

float XMLSerializationNode::getValueFloat() { return node.text().as_float(); }

void XMLSerializationNode::setValueFloat(float f) { node.text().set(f); }

bool XMLSerializationNode::getValueBool() { return node.text().as_bool(); }

void XMLSerializationNode::setValueBool(bool b) { node.text().set(b); }

std::vector<bool> XMLSerializationNode::getValueBoolVector()
{
	std::vector<bool> vec;
	auto string = this->getValue().str();

	vec.resize(string.length());
	for (size_t i = 0; i < string.length(); i++)
	{
		auto c = string[i];
		if (c == '1')
			vec[i] = true;
		else if (c == '0')
			vec[i] = false;
		else
			throw SerializationException(format("Unknown char '%c' in bool vector", c), this);
	}
	return vec;
}

void XMLSerializationNode::setValueBoolVector(const std::vector<bool> &vec)
{
	std::string str(vec.size(), ' ');

	for (size_t i = 0; i < vec.size(); i++)
	{
		auto b = vec[i];
		if (b)
			str[i] = '1';
		else
			str[i] = '0';
	}
	this->setValue(str);
}

UString XMLSerializationNode::getFullPath()
{
	UString str;
	if (this->parent)
	{
		str = this->parent->getFullPath();
	}
	else
	{
		str += node.name();
		str += ".xml:";
	}
	str += "/";
	str += node.name();
	return str;
}

using namespace rapidjson;
class JSONSerializationNode;

enum JSONType 
{
    eJSON_NULL_TYPE,
    eJSON_BOOLEAN_TYPE,
    eJSON_UINT_TYPE,
    eJSON_INT_TYPE,
    eJSON_UINT64_TYPE,
    eJSON_INT64_TYPE,
    eJSON_FLOAT_TYPE,
    eJSON_STRING_TYPE,
    eJSON_CHAR_TYPE,
    eJSON_ROOT_TYPE, //really an object in rapidjson (dict)
    eJSON_ARRAY_TYPE,

};
class JSONSerializationArchive : public SerializationArchive
{
  private:

	up<SerializationDataProvider> dataProvider;
	std::map<UString, up<JSONSerializationNode>> docRoots;
	std::vector<up<SerializationNode>> nodes;
	friend class SerializationArchive;
	friend class JSONSerializationNode;

  public:
	SerializationNode *newRoot(const UString &prefix, const UString &name) override;
	SerializationNode *getRoot(const UString &prefix, const UString &name) override;
	bool write(const UString &path, bool pack, bool pretty) override;
	JSONSerializationArchive() : dataProvider(nullptr), docRoots(){};
	JSONSerializationArchive(up<SerializationDataProvider> dataProvider)
	    : dataProvider(std::move(dataProvider)){};
	~JSONSerializationArchive() override = default;
};

class JSONSerializationNode : public SerializationNode
{
  private:
	SerializationDataProvider *dataProvider;
	JSONSerializationArchive *archive;
    //store the type
    JSONType type;
    //store the key-value pair
    UString name;
    UString value;
    //got to handle me own linked list clean up... gah sorta JGA ??? necessary with archive copy?
	std::vector<up<SerializationNode>> internalNodes;
	JSONSerializationNode *parent;
	UString prefix;

  public:
	JSONSerializationNode(JSONSerializationArchive *archive,
	                     JSONSerializationNode *parent)
	    : archive(archive), type(eJSON_NULL_TYPE), parent(parent)
	{
	}

	JSONSerializationNode(JSONSerializationArchive *archive, const UString &prefix)
	    : archive(archive), type(eJSON_ROOT_TYPE), parent(nullptr), prefix(prefix)
	{
	}

	JSONType getType();
	void setJSONType(JSONType type);
    bool save(const up<Writer<StringBuffer>> &writer);

	SerializationNode *addNode(const UString &name, const UString &value = "") override;
	SerializationNode *addSection(const UString &name) override;

	SerializationNode *getNodeOpt(const UString &name) override;
	SerializationNode *getNextSiblingOpt(const UString &name) override;
	SerializationNode *getSectionOpt(const UString &name) override;

	UString getName() override;
	void setName(const UString &str) override;
	UString getValue() override;
	void setValue(const UString &str) override;

	unsigned int getValueUInt() override;
	void setValueUInt(unsigned int i) override;

	unsigned char getValueUChar() override;
	void setValueUChar(unsigned char i) override;

	int getValueInt() override;
	void setValueInt(int i) override;

	unsigned long long getValueUInt64() override;
	void setValueUInt64(unsigned long long i) override;

	long long getValueInt64() override;
	void setValueInt64(long long i) override;

	float getValueFloat() override;
	void setValueFloat(float f) override;

	bool getValueBool() override;
	void setValueBool(bool b) override;

	std::vector<bool> getValueBoolVector() override;
	void setValueBoolVector(const std::vector<bool> &vec) override;

	UString getFullPath() override;
	const UString &getPrefix() const override
	{
		if (this->parent)
			return this->parent->getPrefix();
		else
			return this->prefix;
	}

	~JSONSerializationNode() override = default;
};


SerializationNode *JSONSerializationArchive::newRoot(const UString &prefix, const UString &name)
{
	auto path = prefix + name + ".json";
    std::unique_ptr<JSONSerializationNode> root(new JSONSerializationNode(this, prefix));
	root->setName(name);
    this->docRoots.insert(std::map<UString, up<JSONSerializationNode>>::value_type(path, root));
    //this is part of the serialization archive interface so stays other than fixing root JGA
	this->nodes.push_back(mkup<JSONSerializationNode>(this, root, prefix + name + "/"));
    //grab pointer just inserted into list
	return this->nodes.back().get();
}

SerializationNode *JSONSerializationArchive::getRoot(const UString &prefix, const UString &name)
{
	auto path = prefix + name + ".json";
	if (dataProvider == nullptr)
	{
		LogWarning("Reading from not opened archive: %s!", path);
		return nullptr;
	}

    //hm intesting puginess xml_documet JGA
	auto it = this->docRoots.find(path);
	if (it == this->docRoots.end())
	{
		TraceObj trace("Reading archive", {{"path", path}});
		UString content;
		if (dataProvider->readDocument(path, content))
		{
            //PUGI PUGI JGA, how do we get the root in JSON Document?
			// FIXME: Make this actually read from the root and load the xinclude tags properly?
			auto &doc = this->docRoots[path]; ///does this add new docroot? map feature of C++ ugly, not explicit JGA
			TraceObj traceParse("Parsing archive", {{"path", path}});
            //ugh heres the actually xml parse here from the file content JGA
			auto parse_result = doc.load_string(content.cStr());
			if (!parse_result)
			{
				LogInfo("Failed to parse \"%s\" : \"%s\" at \"%llu\"", path,
				        parse_result.description(), (unsigned long long)parse_result.offset);
				return nullptr;
			}
            //reset iterator to new doc root in map JGA
			it = this->docRoots.find(path);
			LogInfo("Parsed \"%s\"", path);
		}
	}
    //is this really an error? JGA
	if (it == this->docRoots.end())
	{
		return nullptr;
	}

    //i think is grab the root serialization node of this doc root to insert into
    //node tree JGA
	auto root = it->second.child(name.cStr());
	if (!root)
	{
		LogWarning("Failed to find root with name \"%s\" in \"%s\"", name, path);
		return nullptr;
	}
	this->nodes.push_back(mkup<JSONSerializationNode>(this, root, prefix + name + "/"));
    //grab last node inserted? JGA isn't this already in root above? --- no, root is iterator, this is the actual pointer
	return this->nodes.back().get();
}

bool JSONSerializationArchive::write(const UString &path, bool pack, bool pretty)
{
	TraceObj trace("Writing archive", {{"path", path}});
	// warning! data provider must be freed when this method ends,
	// so code calling this method may override archive
	auto dataProvider = getProvider(pack);
	if (!dataProvider->openArchive(path, true))
	{
		LogWarning("Failed to open archive at \"%s\"", path);
		return false;
	}

	for (auto &root : this->docRoots)
	{
		TraceObj traceSave("Saving root", {{"root", root.first}});
		StringBuffer ss;
        up<Writer<StringBuffer>> writer;
		if (pretty == false)
		{
            writer = up<Writer<StringBuffer>> (new Writer<StringBuffer>(ss));
		}
        else
        {
            writer = up<Writer<StringBuffer>> (new PrettyWriter<StringBuffer>(ss));
        }
		root.second->save(writer);///actual writer part JGA
		TraceObj traceSaveData("Saving root data", {{"root", root.first}});
		if (!dataProvider->saveDocument(root.first, ss.GetString()))
		{
			return false;
		}
	}

	return dataProvider->finalizeSave();
}

SerializationNode *JSONSerializationNode::addNode(const UString &name, const UString &value)
{
    std::unique_ptr<JSONSerializationNode> newNode(new JSONSerializationNode(this->archive, this));
	newNode->setName(name);
    newNode->setValue(value);
    //what about setting the type???
    //add to node list JGA
    this->internalNodes.push_back(mkup<JSONSerializationNode>(newNode));
	this->archive->nodes.push_back(mkup<JSONSerializationNode>(this->archive, newNode, this));
	return this->archive->nodes.back().get();
}

SerializationNode *JSONSerializationNode::getNodeOpt(const UString &name)
{
    return nullptr;
}

SerializationNode *JSONSerializationNode::getNextSiblingOpt(const UString &name)
{
    return nullptr;
}

SerializationNode *JSONSerializationNode::addSection(const UString &name)
{
    //make a key that is the path+filename with no value? NULL() or just include string JGA
	auto includeNode = static_cast<JSONSerializationNode *>(this->addNode(name + ".json"));
    if(includeNode == nullptr)
        return nullptr;
	return this->archive->newRoot(this->getPrefix(), name);
}

SerializationNode *JSONSerializationNode::getSectionOpt(const UString &name)
{
    return nullptr;
}

UString JSONSerializationNode::getName() { return name; }

void JSONSerializationNode::setName(const UString &str) { name = str; }

UString JSONSerializationNode::getValue() { return value; }

void JSONSerializationNode::setValue(const UString &str) { type = eJSON_STRING_TYPE; value = str; }

unsigned int JSONSerializationNode::getValueUInt() { return node.text().as_uint(); }

void JSONSerializationNode::setValueUInt(unsigned int i) { type = eJSON_STRING_TYPE; value = UString(i); }

unsigned char JSONSerializationNode::getValueUChar()
{
	auto uint = node.text().as_uint();
	if (uint > std::numeric_limits<unsigned char>::max())
	{
		throw SerializationException(format("Value %u is out of range of unsigned char type", uint),
		                             this);
	}
	return static_cast<unsigned char>(uint);
}

void JSONSerializationNode::setValueUChar(unsigned char c) { type = eJSON_CHAR_TYPE; value = UString(c);}

int JSONSerializationNode::getValueInt() { return node.text().as_int(); }

void JSONSerializationNode::setValueInt(int i) { type = eJSON_INT_TYPE; value = UString(i);}

unsigned long long JSONSerializationNode::getValueUInt64() { return node.text().as_ullong(); }

void JSONSerializationNode::setValueUInt64(unsigned long long i) { type = eJSON_UINT64_TYPE; value = UString(i);}

long long JSONSerializationNode::getValueInt64() { return node.text().as_llong(); }

void JSONSerializationNode::setValueInt64(long long i) { type = eJSON_INT64_TYPE; value = UString(i);}

float JSONSerializationNode::getValueFloat() { return node.text().as_float(); }

void JSONSerializationNode::setValueFloat(float f) { type = eJSON_FLOAT_TYPE; value = UString(f);}

bool JSONSerializationNode::getValueBool() { return node.text().as_bool(); }

void JSONSerializationNode::setValueBool(bool b) { type = eJSON_BOOLEAN_TYPE; value = UString(b);}

std::vector<bool> JSONSerializationNode::getValueBoolVector()
{
	std::vector<bool> vec;
	auto string = this->getValue().str();

	vec.resize(string.length());
	for (size_t i = 0; i < string.length(); i++)
	{
		auto c = string[i];
		if (c == '1')
			vec[i] = true;
		else if (c == '0')
			vec[i] = false;
		else
			throw SerializationException(format("Unknown char '%c' in bool vector", c), this);
	}
	return vec;
}

void JSONSerializationNode::setValueBoolVector(const std::vector<bool> &vec)
{
    type = eJSON_ARRAY_TYPE;
	std::string str(vec.size(), ' ');

	for (size_t i = 0; i < vec.size(); i++)
	{
		auto b = vec[i];
		if (b)
			str[i] = '1';
		else
			str[i] = '0';
	}
	this->setValue(str);
}

UString JSONSerializationNode::getFullPath()
{
	UString str;
	if (this->parent)
	{
		str = this->parent->getFullPath();
	}
	else
	{
		str += name;
		str += ".json:"; //JGA this may not work with the way I named root
	}
	str += "/";
	str += name;
	return str;
}

//JGA got to fix this for JSON 

up<SerializationArchive> SerializationArchive::createArchive()
{
	return mkup<XMLSerializationArchive>();
}

up<SerializationArchive> SerializationArchive::readArchive(const UString &name)
{
	up<SerializationDataProvider> dataProvider = getProvider(!fs::is_directory(name.str()));
	if (!dataProvider->openArchive(name, false))
	{
		LogWarning("Failed to open archive at \"%s\"", name);
		return nullptr;
	}
	LogInfo("Opened archive \"%s\"", name);

	return mkup<XMLSerializationArchive>(std::move(dataProvider));
}

} // namespace OpenApoc

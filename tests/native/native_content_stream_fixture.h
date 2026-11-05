#ifndef QUANTAPDF_NATIVE_CONTENT_STREAM_FIXTURE_H
#define QUANTAPDF_NATIVE_CONTENT_STREAM_FIXTURE_H
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFWriter.hh>
#include <zlib.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace native_content_fixture {
using Object=QPDFObjectHandle;
struct original {std::string bytes;size_t decoded_bytes=0u,streams=0u;};
inline std::string flate(const std::string& input)
{
    uLongf size=compressBound(static_cast<uLong>(input.size()));
    std::string output(size,'\0');
    if(compress2(reinterpret_cast<Bytef*>(output.data()),&size,
        reinterpret_cast<const Bytef*>(input.data()),static_cast<uLong>(input.size()),9)!=Z_OK)
        throw std::runtime_error("content fixture Flate encoding failed");
    output.resize(size);return output;
}
inline std::string hex(const std::string& input)
{
    const char *digits="0123456789ABCDEF";
    std::string output;
    for(unsigned char c:input) {output+=digits[c>>4u];output+=digits[c&15u];}
    output+='>';return output;
}
inline std::string ascii85(const std::string& input)
{
    std::string output;
    for(size_t i=0u;i<input.size();i+=4u) {
        const size_t count=std::min(size_t(4),input.size()-i);
        uint32_t value=0u;
        for(size_t j=0u;j<4u;++j)value=(value<<8u)|(j<count?static_cast<unsigned char>(input[i+j]):0u);
        char group[5];
        for(size_t j=5u;j>0u;--j) {group[j-1u]=char(value%85u+33u);value/=85u;}
        output.append(group,count+1u);
    }
    output+="~>";return output;
}
inline std::string run_length(const std::string& input)
{
    std::string output;
    for(size_t i=0u;i<input.size();i+=128u) {
        const size_t count=std::min(size_t(128),input.size()-i);
        output+=char(count-1u);output.append(input,i,count);
    }
    output+=char(128);return output;
}
inline Object stream(QPDF& pdf,const std::string& decoded,unsigned encoding)
{
    std::string bytes=decoded;
    auto filter=Object::newNull(),parms=Object::newNull();
    switch(encoding) {
    case 0:break;
    case 1:bytes=flate(bytes);filter=Object::newName("/FlateDecode");break;
    case 2:bytes=hex(bytes);filter=Object::newName("/ASCIIHexDecode");break;
    case 3:bytes=ascii85(bytes);filter=Object::newName("/ASCII85Decode");break;
    case 4:bytes=run_length(bytes);filter=Object::newName("/RunLengthDecode");break;
    case 5:bytes=hex(flate(bytes));filter=Object::parse("[/ASCIIHexDecode /FlateDecode]");
        parms=Object::parse("[null null]");break;
    case 6:bytes=flate(bytes);filter=Object::parse("[/FlateDecode]");parms=Object::parse("[null]");break;
    case 7:bytes=ascii85(run_length(flate(bytes)));
        filter=Object::parse("[/ASCII85Decode /RunLengthDecode /FlateDecode]");
        parms=Object::parse("[null null null]");break;
    default:throw std::runtime_error("invalid content fixture encoding");
    }
    auto result=pdf.newStream();result.replaceStreamData(bytes,filter,parms);return result;
}
inline original create(unsigned encoding,size_t count,bool encrypted=false,int invalid=0,size_t extra_bytes=0u)
{
    auto pdf=QPDF::create();pdf->emptyPDF();
    auto root=pdf->getRoot().getKey("/Pages");
    auto page=pdf->makeIndirectObject(Object::parse("<< /Type /Page /MediaBox [0 0 12 9] >>"));
    page.replaceKey("/Parent",root);root.replaceKey("/Kids",Object::newArray({page}));
    root.replaceKey("/Count",Object::newInteger(1));
    const std::string prefix="q 1 0 0 rg\n",left="q 0 0 1 rg 0 0 6 9 re f Q\n",right="6 0 6 9 re f Q\n";
    original result;result.streams=count;
    std::vector<Object> streams;
    for(size_t i=0u;i<count;++i) {
        std::string decoded=count==1u?prefix+left+right:
            i==0u?prefix:i==1u?left:i==count-1u?right:std::string();
        if(count>3u)decoded=std::string(5000u,' ')+decoded;
        if(invalid==1 && i==count-1u)decoded+="Q\n";
        if(invalid==2 && i==count-1u)decoded+="BT 1 Tr ET\n";
        if(decoded.empty() || decoded.back()!='\n')decoded+='\n';
        result.decoded_bytes+=decoded.size();
        streams.push_back(stream(*pdf,decoded,(encoding+unsigned(i))%8u));
    }
    if(invalid==3)streams.back().replaceStreamData("invalid deflate data",Object::newName("/FlateDecode"),Object::newNull());
    if(invalid==5)streams.back().replaceStreamData(right,Object::newName("/UnknownDecode"),Object::newNull());
    if(invalid==6)streams.back().getDict().replaceKey("/F",Object::newString("external-content.pdf"));
    if(invalid==7)streams.back().replaceStreamData(right,Object::parse("[17]"),Object::newNull());
    if(invalid==4)streams.push_back(Object::newInteger(17));
    page.replaceKey("/Contents",count==1u?streams[0]:Object::newArray(streams));
    if(extra_bytes!=0u)pdf->getRoot().replaceKey("/QuantaFixtureData",
        stream(*pdf,std::string(extra_bytes,'x'),0u));
    QPDFWriter writer(*pdf);writer.setOutputMemory();
    writer.setStreamDataMode(qpdf_s_preserve);writer.setCompressStreams(false);
    if(encrypted)writer.setR6EncryptionParameters("reader","owner",true,true,true,true,true,true,qpdf_r3p_full,true);
    writer.write();const auto buffer=writer.getBufferSharedPointer();
    result.bytes.assign(reinterpret_cast<const char*>(buffer->getBuffer()),buffer->getSize());
    return result;
}
}
#endif

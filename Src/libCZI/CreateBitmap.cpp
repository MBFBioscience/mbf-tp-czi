// SPDX-FileCopyrightText: 2017-2022 Carl Zeiss Microscopy GmbH
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "bitmapData.h"
#include "Site.h"
#include "libCZI.h"
#include "BitmapOperations.h"
#include "inc_libCZI_Config.h"
#include "CziSubBlock.h"

using namespace libCZI;

// Forward declaration for JpgXr vs. uncompressed SubBlocks workaround.
static std::shared_ptr<libCZI::IBitmapData> CreateBitmapFromSubBlock_Uncompressed(ISubBlock* subBlk);

static std::shared_ptr<libCZI::IBitmapData> CreateBitmapFromSubBlock_JpgXr(ISubBlock* subBlk)
{
    auto dec = GetSite()->GetDecoder(ImageDecoderType::JPXR_JxrLib, nullptr);
    const void* ptr; size_t size;
    subBlk->DangerousGetRawData(ISubBlock::MemBlkType::Data, ptr, size);
	
	// Workaround for malformed CZI files which have SubBlocks labeled
	// as JpxXr in the SubBlockDirectory (verified in the CZI binary
	// file with hexdump), but which do not have the correct JpxXr
	// header magic (hex 49 49 BC 01, hexdump verified as present in
	// wellformed JpxXr SubBlocks, missing in malformed ones), and are
	// in fact uncompressed binary image data (with size agreeing with
	// uncompressed pixel data size, and image verified by rendering
	// the malformed SubBlocks as uncompressed images).
	//
	static const unsigned char jpgxr_header_magic[] = { 0x49, 0x49, 0xbc, 0x01 };
	if (memcmp((unsigned char*)ptr, jpgxr_header_magic, 4) != 0
		&& (size == ((size_t)subBlk->GetSubBlockInfo().physicalSize.h *
		(size_t)subBlk->GetSubBlockInfo().physicalSize.w *
			(size_t)CziUtils::GetBytesPerPel(subBlk->GetSubBlockInfo().pixelType)))) {
		return CreateBitmapFromSubBlock_Uncompressed(subBlk);
	}
	
    SubBlockInfo subBlockInfo = subBlk->GetSubBlockInfo();
    return dec->Decode(ptr, size, subBlockInfo.pixelType, subBlockInfo.physicalSize.w, subBlockInfo.physicalSize.h);
}

static std::shared_ptr<libCZI::IBitmapData> CreateBitmapFromSubBlock_ZStd0(ISubBlock* subBlk)
{
    auto dec = GetSite()->GetDecoder(ImageDecoderType::ZStd0, nullptr);
    const void* ptr; size_t size;
    subBlk->DangerousGetRawData(ISubBlock::MemBlkType::Data, ptr, size);
    SubBlockInfo subBlockInfo = subBlk->GetSubBlockInfo();
    return dec->Decode(ptr, size, subBlockInfo.pixelType, subBlockInfo.physicalSize.w, subBlockInfo.physicalSize.h);
}

static std::shared_ptr<libCZI::IBitmapData> CreateBitmapFromSubBlock_ZStd1(ISubBlock* subBlk)
{
    auto dec = GetSite()->GetDecoder(ImageDecoderType::ZStd1, nullptr);
    const void* ptr; size_t size;
    subBlk->DangerousGetRawData(ISubBlock::MemBlkType::Data, ptr, size);
    SubBlockInfo subBlockInfo = subBlk->GetSubBlockInfo();

    return dec->Decode(ptr, size, subBlockInfo.pixelType, subBlockInfo.physicalSize.w, subBlockInfo.physicalSize.h);
}

static std::shared_ptr<libCZI::IBitmapData> CreateBitmapFromSubBlock_Uncompressed(ISubBlock* subBlk)
{
    size_t size;
    CSharedPtrAllocator sharedPtrAllocator(subBlk->GetRawData(ISubBlock::MemBlkType::Data, &size));

    const auto& sbBlkInfo = subBlk->GetSubBlockInfo();

    // The stride with an uncompressed bitmap in CZI is exactly the linesize.
    const std::uint32_t stride = sbBlkInfo.physicalSize.w * CziUtils::GetBytesPerPel(sbBlkInfo.pixelType);
    if (static_cast<size_t>(stride) * sbBlkInfo.physicalSize.h > size)
    {
        throw std::logic_error("insufficient size of subblock");
    }

    auto sb = CBitmapData<CSharedPtrAllocator>::Create(
        sharedPtrAllocator,
        sbBlkInfo.pixelType,
        sbBlkInfo.physicalSize.w,
        sbBlkInfo.physicalSize.h,
        stride);

#if LIBCZI_ISBIGENDIANHOST
    if (!CziUtils::IsPixelTypeEndianessAgnostic(subBlk->GetSubBlockInfo().pixelType))
    {
        return CBitmapOperations::ConvertToBigEndian(sb.get());
    }
#endif

    return sb;
}

std::shared_ptr<libCZI::IBitmapData> libCZI::CreateBitmapFromSubBlock(ISubBlock* subBlk)
{
    switch (subBlk->GetSubBlockInfo().GetCompressionMode())
    {
    case CompressionMode::JpgXr:
        return CreateBitmapFromSubBlock_JpgXr(subBlk);
    case CompressionMode::Zstd0:
        return CreateBitmapFromSubBlock_ZStd0(subBlk);
    case CompressionMode::Zstd1:
        return CreateBitmapFromSubBlock_ZStd1(subBlk);
    case CompressionMode::UnCompressed:
        return CreateBitmapFromSubBlock_Uncompressed(subBlk);
    default:    // silence warnings
        throw std::logic_error("The method or operation is not implemented.");
    }
}

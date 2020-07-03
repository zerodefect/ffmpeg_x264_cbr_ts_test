
#include <functional>
#include <iostream>
#include <memory>
#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

//////////////////////////////////////////////////////////////////////////
std::string error_code_to_string(const int nErrCode)
{
    char chArray[AV_ERROR_MAX_STRING_SIZE];

    // GAV: Probably better to be on the safe size and initialise the array.
    // The docs do not state if the string is null terminated :(
    std::fill(std::begin(chArray), std::end(chArray), '\0');

    if (av_strerror(nErrCode, chArray, AV_ERROR_MAX_STRING_SIZE) != 0)
    {
        return "[Unknown]";
    }

    return std::string(chArray);
}

///////////////////////////////////////////////////////////////////////////
bool open_input_format_context(const std::string &strPath,
                               std::unique_ptr<AVFormatContext, std::function<void (AVFormatContext*)>> &out_apFmtCtx)
{
    out_apFmtCtx.reset();

    AVFormatContext *pFmtCtx = nullptr;

    // Open input file, and allocate format context
    if (int nRet = avformat_open_input(&pFmtCtx,
                                       strPath.c_str(),
                                       nullptr,
                                       nullptr); nRet < 0)
    {
        std::cerr << "Could not open media at path: '"
                  << strPath
                  << "': "
                  << error_code_to_string(nRet)
                  << std::endl;

        return false;
    }

    out_apFmtCtx = std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>>{pFmtCtx,
                                                                                            [](AVFormatContext *pFmtCtx)
                                                                                            {
                                                                                                // Not sure this function can take a nullptr!
                                                                                                if (pFmtCtx != nullptr)
                                                                                                {
                                                                                                    avformat_close_input(&pFmtCtx);
                                                                                                }
                                                                                            }};
    return true;
}

///////////////////////////////////////////////////////////////////////////
bool open_output_format_context(const std::string &strMediaPath,
                                std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>> &out_apFmtCtx)
{
    out_apFmtCtx.reset();

    AVFormatContext *pFmtCtx = nullptr;
    if (int nRet = avformat_alloc_output_context2(&pFmtCtx, nullptr, nullptr, strMediaPath.c_str()); nRet < 0)
    {
        std::cerr << "Could not create media context for media at path: '"
                  << strMediaPath
                  << "': "
                  << error_code_to_string(nRet);

        return false;
    }

    out_apFmtCtx = std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>>{pFmtCtx,
                                                                                            [](AVFormatContext *pFmtCtx)
                                                                                            {
                                                                                                // Not sure this function can take a nullptr!
                                                                                                if (pFmtCtx != nullptr)
                                                                                                {
                                                                                                    avformat_free_context(pFmtCtx);
                                                                                                }
                                                                                            }};

    return true;
}

//////////////////////////////////////////////////////////////////////////
bool open_decoder_context(AVFormatContext *const in_pFmtCtx,
                          const AVMediaType in_eMediaType,
                          int &inout_nStreamidx,
                          std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> &inout_apCdcCtx,
                          const std::function<bool(AVCodecContext *)> &fnInitContext)
{
    const int nDesiredIdx = std::max<int>(inout_nStreamidx,-1);

    // Reset in case something goes wrong.
    inout_nStreamidx = -1;
    inout_apCdcCtx.reset();

    if (!in_pFmtCtx)
    {
        std::cerr << "Pointer to format context is NULL" << std::endl;
        return false;
    }

    int ret = av_find_best_stream(in_pFmtCtx, in_eMediaType, nDesiredIdx, -1, nullptr, 0);
    if (ret < 0)
    {
        std::cerr << "Could not find '"
                  << av_get_media_type_string(in_eMediaType)
                  << "' stream in input file ("
                  << ret
                  << "): "
                  << error_code_to_string(ret)
                  << std::endl;

        return false;
    }

    const int stream_index = ret;
    AVStream *const st = in_pFmtCtx->streams[stream_index];

    // Find decoder for the stream
    AVCodec *const pCdc = avcodec_find_decoder(st->codecpar->codec_id);
    if (pCdc == nullptr)
    {
        std::cerr << "Failed to find '"
                  << av_get_media_type_string(in_eMediaType)
                  << "' decoder codec"
                  << std::endl;
        return false;
    }

    // Allocate a codec context for the decoder
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> apCdcCtx
        (
            avcodec_alloc_context3(pCdc),
            [](AVCodecContext *pCdcCtx) mutable
            {
                // Not sure this function can take a nullptr!
                if (pCdcCtx != nullptr)
                {
                    avcodec_free_context(&pCdcCtx);
                }
            }
        );

    if (!apCdcCtx)
    {
        std::cerr << "Failed to allocate the '"
                  << av_get_media_type_string(in_eMediaType)
                  << "' decoder codec context."
                  << std::endl;
        return false;
    }

    // Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(apCdcCtx.get(), st->codecpar)) < 0)
    {
        std::cerr << "Failed to copy '"
                  << av_get_media_type_string(in_eMediaType)
                  << "' decoder codec parameters to decoder context: "
                  << error_code_to_string(ret)
                  << std::endl;

        return false;
    }

    if (!!fnInitContext)
    {
        if (!fnInitContext(apCdcCtx.get()))
        {
            std::cerr << "Failed to initialise decoder codec context using custom initialisation function."
                      << std::endl;
            return false;
        }
    }

    // Initialise the decoders, with or without reference counting
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "refcounted_frames", "1", 0);
    if ((ret = avcodec_open2(apCdcCtx.get(), pCdc, &opts)) < 0)
    {
        std::cerr << "Failed to open '"
                  << av_get_media_type_string(in_eMediaType)
                  << "' decoder codec: "
                  << error_code_to_string(ret)
                  << std::endl;

        return false;
    }

    inout_apCdcCtx = std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>>
        (
            apCdcCtx.release(),
            [](AVCodecContext *pCdcCtx) mutable
            {
                // Not sure this function can take a nullptr!
                if (pCdcCtx != nullptr)
                {
                    avcodec_free_context(&pCdcCtx);
                }
            }
        );
    inout_nStreamidx = stream_index;
    return true;
}

//////////////////////////////////////////////////////////////////////////
bool open_encoder_context(AVFormatContext *const in_pFmtCtx,
                          std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> &inout_apCdcCtx,
                          AVStream *&inout_pStream,
                          const std::string &in_strCodecId,
                          const std::function<bool(AVStream * , AVCodecContext * , AVDictionary * &)> &fnInitContext)
{
    inout_apCdcCtx.reset();
    inout_pStream = nullptr;

    if (!in_pFmtCtx)
    {
        std::cerr << "Pointer to format context is NULL" << std::endl;
        return false;
    }

    // Find the encoder
    auto pCdc = avcodec_find_encoder_by_name(in_strCodecId.c_str());
    if (pCdc == nullptr)
    {
        std::cerr << "Failed to find encoder codec '" << in_strCodecId << "'" << std::endl;
        return false;
    }

    auto pStream = avformat_new_stream(in_pFmtCtx, nullptr);
    if (pStream == nullptr)
    {
        std::cerr << "Could not allocate elementary stream" << std::endl;
        return false;
    }

    pStream->id = in_pFmtCtx->nb_streams - 1;

    // Allocate a codec context for the encoder
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> apCdcCtx
        (
            avcodec_alloc_context3(pCdc),
            [](AVCodecContext *pCdcCtx) mutable
            {
                avcodec_free_context(&pCdcCtx);
            }
        );

    // Allocate a codec context for the encoder
    if (!apCdcCtx)
    {
        std::cerr << "Failed to allocate the '"
                  << av_get_media_type_string(pCdc->type)
                  << "' encoder codec context."
                  << std::endl;
        return false;
    }

    AVDictionary *pDict = nullptr;
    if (fnInitContext)
    {
        if (!fnInitContext(pStream, apCdcCtx.get(), pDict))
        {
            std::cerr << "Failed to initialise encoder codeccontext using custom initialisation function." << std::endl;
            return false;
        }
    }

    // Some formats want stream headers to be separate.
    if (in_pFmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        apCdcCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Init the encoder
    {
        const int ret = avcodec_open2(apCdcCtx.get(), pCdc, &pDict);
        if (ret < 0)
        {
            std::cerr << "Failed to open '"
                      << av_get_media_type_string(apCdcCtx->codec_type)
                      << "' encoder codec: "
                      << error_code_to_string(ret)
                      << std::endl;

            return false;
        }
    }

    // Fill the parameters struct based on the values from the supplied codec context. This
    // sets the parameters in the muxer.
    {
        const int ret = avcodec_parameters_from_context(pStream->codecpar, apCdcCtx.get());
        if (ret < 0)
        {
            std::cerr << "Could not copy the encoder context stream parameters to the multiplexer"
                      << std::endl;
            return false;
        }
    }

    inout_apCdcCtx = std::move(apCdcCtx);
    inout_pStream = pStream;
    return true;
}

//////////////////////////////////////////////////////////////////////////
int get_next_decoder_frame(AVFormatContext *pFmtCtxIn,
                           AVCodecContext *pCdcCtxIn,
                           int nStreamIdx,
                           AVPacket *pPkt,
                           bool &bPendingPkt,
                           std::shared_ptr<AVFrame> &inout_apFrameOut)
{
    while (true)
    {
        int ret = bPendingPkt ? 0 : av_read_frame(pFmtCtxIn, pPkt);
        if (ret < 0)
        {
            // This is probably EOF?? Tell our decoder EOF
            avcodec_send_packet(pCdcCtxIn, nullptr);
        }
        else
        {
            // Make sure this is our video index.
            if (!bPendingPkt && pPkt->stream_index != nStreamIdx)
            {
                av_packet_unref(pPkt);
                continue;
            }

            // 1. Send packet
            if (int retInner = avcodec_send_packet(pCdcCtxIn, pPkt); ret != 0)
            {
                if (retInner == AVERROR(EAGAIN))
                {
                    bPendingPkt = true;
                }
                else
                {
                    std::cerr << "Unexpected error received from decoder (avcodec_send_packet): "
                              << error_code_to_string(retInner)
                              << ". Cannot continue."
                              << std::endl;
                    return retInner;
                }
            }
            else
            {
                bPendingPkt = false;
            }
        }

        std::shared_ptr<AVFrame> apAVFrame{av_frame_alloc(),
                                           [](AVFrame *p)
                                           {
                                               if (p != nullptr)
                                               {
                                                   av_frame_free(&p);
                                               }
                                           }};

        // 2. Receive frame
        if (int ret = avcodec_receive_frame(pCdcCtxIn, apAVFrame.get()); ret != 0)
        {
            if (ret == AVERROR_EOF)
            {
                // We are done here.
                return ret;
            }
            if (ret == AVERROR(EAGAIN))
            {
                // Go around again.
            }
            else
            {
                std::cerr << "Unexpected error received from decoder (avcodec_receive_frame): "
                          << error_code_to_string(ret)
                          << ". Cannot continue."
                          << std::endl;

                // Not sure of best error code to return, but
                // let us go with this.
                return AVERROR(EFAULT);
            }
        }
        else
        {
//            std::cout << "Decoded frame - PTS="
//                      << apAVFrame->pts
//                      << ", DTS="
//                      << pPkt->dts
//                      << std::endl;

            apAVFrame->pts = AV_NOPTS_VALUE;
            apAVFrame->pkt_dts = AV_NOPTS_VALUE;
            apAVFrame->pkt_pos = -1;
            apAVFrame->pkt_size = -1;
            apAVFrame->pkt_duration = 0;

            inout_apFrameOut = std::move(apAVFrame);
            return 0;
        }
    }
}


//////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    if (argc < 1)
    {
        std::cerr << "Argument to input AV file is required: "
                  << "./x264_cbr [file_in] [file_out]"
                  << std::endl;
        return 1;
    }

    //av_log_set_level(AV_LOG_DEBUG);

    const std::string strSrcFilename = argv[1];

    std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>> apFmtCtxIn;
    if (!open_input_format_context(argv[1], apFmtCtxIn))
    {
        std::cerr << "Could not open source file " << strSrcFilename << std::endl;
        return 1;
    }

    std::unique_ptr<AVFormatContext, std::function<void(AVFormatContext *)>> apFmtCtxOut;
    if (!open_output_format_context(argv[2], apFmtCtxOut))
    {
        std::cerr << "Could not open destination file " << strSrcFilename << std::endl;
        return 1;
    }

    // Open file if required.
    if (!(apFmtCtxOut->oformat->flags & AVFMT_NOFILE))
    {
        int ret = avio_open(&apFmtCtxOut->pb, argv[2], AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            std::cerr << "Could not open output file "
                      << argv[2]
                      << std::endl;
            return ret;
        }
    }

    int nStreamIdxIn{};
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> apCdcCtxIn;
    if (!open_decoder_context(apFmtCtxIn.get(), AVMEDIA_TYPE_VIDEO, nStreamIdxIn, apCdcCtxIn, {}))
    {
        std::cerr << "Failed to open decoder context" << std::endl;
        return 1;
    }

    AVStream *pStVideoIn = apFmtCtxIn->streams[nStreamIdxIn];

    AVStream *pStVideoOut{};
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> apCdcCtxOut;
    if (!open_encoder_context(apFmtCtxOut.get(),
                              apCdcCtxOut,
                              pStVideoOut,
                              "libx264",
                              [pCdcCtxIn=apCdcCtxIn.get(), pStVideoIn](AVStream *stVideoOut, AVCodecContext *pCdcCtxOut, AVDictionary *&pDict) -> bool
                              {
                                  av_dict_set(&pDict, "preset", "faster", 0);
                                  av_dict_set(&pDict, "tune", "film", 0);
                                  av_dict_set_int(&pDict, "rc-lookahead", 25, 0);

                                  pCdcCtxOut->width = pCdcCtxIn->width;
                                  pCdcCtxOut->height = pCdcCtxIn->height;
                                  pCdcCtxOut->pix_fmt = AV_PIX_FMT_YUV420P;
                                  pCdcCtxOut->gop_size = 25;

                                  // Going for 6Mbit/s
                                  pCdcCtxOut->bit_rate = 6000000;
                                  //pCdcCtxOut->rc_min_rate = pCdcCtxOut->bit_rate;
                                  pCdcCtxOut->rc_max_rate = pCdcCtxOut->bit_rate;
                                  pCdcCtxOut->rc_buffer_size = pCdcCtxOut->bit_rate;
                                  pCdcCtxOut->rc_initial_buffer_occupancy = static_cast<int>((pCdcCtxOut->bit_rate * 9) / 10);

                                  std::string strParams = "vbv-maxrate="
                                                          + std::to_string(pCdcCtxOut->bit_rate / 1000)
                                                          + ":vbv-bufsize="
                                                          + std::to_string(pCdcCtxOut->bit_rate / 1000)
                                                          + ":force-cfr=1:nal-hrd=cbr";

                                  av_dict_set(&pDict, "x264-params", strParams.c_str(), 0);

                                  pCdcCtxOut->field_order = AV_FIELD_TT;
                                  pCdcCtxOut->flags = (AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME | AV_CODEC_FLAG_CLOSED_GOP);

                                                   // WARN: Make some assumtpions here!
                                  pCdcCtxOut->time_base = AVRational{1,25};
                                  pCdcCtxOut->framerate = AVRational{25,1};
                                  pCdcCtxOut->sample_aspect_ratio = AVRational{64,45};

                                  return true;
                              }))
    {
        std::cerr << "Could not open encoder output" << std::endl;
        return 1;
    }

    AVDictionary *pDict{};
    av_dict_set(&pDict, "muxrate", "6300000", 0);
    av_dict_set(&pDict, "max_delay", "6000000", 0);

    // Init muxer, write output file header
    if (int ret = avformat_write_header(apFmtCtxOut.get(), &pDict); ret < 0)
    {
        std::cerr << "Error occurred when opening output file: "
                  << error_code_to_string(ret)
                  << std::endl;
        return ret;
    }



    AVPacket pktDec{};
    av_init_packet(&pktDec);

    int64_t nTimebase{0};

    bool bPendingPktDemux{false};
    std::shared_ptr<AVFrame> apAVFrame{};
    while (true)
    {
        const auto getNextFrame = apAVFrame
            ? 0
            : get_next_decoder_frame(apFmtCtxIn.get(), apCdcCtxIn.get(), nStreamIdxIn, &pktDec, bPendingPktDemux, apAVFrame);

        if (getNextFrame == AVERROR_EOF)
        {
            // Signal EOF
            avcodec_send_frame(apCdcCtxOut.get(), nullptr);
        }
        else if (getNextFrame < 0)
        {
            break;
        }
        else if (!!apAVFrame)
        {
            apAVFrame->pts = nTimebase;

            apAVFrame->key_frame = 0;
            apAVFrame->pict_type = AV_PICTURE_TYPE_NONE;

            std::cout << "avcodec_send_frame: PTS="
                      << apAVFrame->pts
                      << std::endl;

            if (int ret = avcodec_send_frame(apCdcCtxOut.get(), apAVFrame.get()); ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                {
                    std::cerr << "Unexpected error detected while sending frame to encoder. Cannot continue. Error: "
                              << error_code_to_string(ret);
                }
            }
            else
            {
//                std::cout << "avcodec_send_frame: CONSUMED"
//                          << std::endl;

                nTimebase += apCdcCtxOut->time_base.num;
                apAVFrame.reset();
            }
        }


        AVPacket pktEnc{};
        av_init_packet(&pktEnc);
        if (int ret = avcodec_receive_packet(apCdcCtxOut.get(), &pktEnc); ret != 0)
        {
            av_packet_unref(&pktEnc);

            if (ret == AVERROR(EAGAIN))
            {

            }
            else if (ret == AVERROR_EOF)
            {
                break;
            }
            else
            {
                std::cerr << "Unexpected error received packet from encoder. Cannot continue. Error: "
                          << error_code_to_string(ret)
                          << std::endl;
                break;
            }
        }
        else
        {
            av_packet_rescale_ts(&pktEnc, apCdcCtxOut->time_base, pStVideoOut->time_base);

            std::cout << "Written packet, PTS= "
                      << pktEnc.pts
                      << ", DTS="
                      << pktEnc.dts
                      << std::endl;

            if (int retInner = av_interleaved_write_frame(apFmtCtxOut.get(), &pktEnc); retInner != 0)
            {
                av_packet_unref(&pktEnc);

                std::cerr << "Unexpected error writing packet to IO. Cannot continue. Error: "
                          << error_code_to_string(retInner)
                          << std::endl;
                break;
            }

            av_packet_unref(&pktEnc);
        }
    }

    av_write_trailer(apFmtCtxOut.get());

    // close output
    if (apFmtCtxOut && !(apFmtCtxOut->flags & AVFMT_NOFILE))
    {
        avio_closep(&apFmtCtxOut->pb);
    }

    return 0;
}

/// Iterates over fixed-sized consecutive subsequences of a given
/// collection. The final chunk may be smaller than the provided chunk size.
///
/// - Example:
///
/// ```
/// (0..<100).chunks(ofSize: 17)
///
/// // 0..<17
/// // 17..<34
/// // 34..<51
/// // 51..<68
/// // 68..<85
/// // 85..<100
/// ```
///
/// If the collection being chunked is empty, the chunks collection will
/// also be empty.
public struct Chunks<Data: Collection>: RandomAccessCollection {
    public typealias Element = Data.SubSequence

    var chunkSize: Int
    var collection: Data

    public init(_ collection: Data, chunkSize: Int) {
        precondition(chunkSize > 0)
        self.chunkSize = chunkSize
        self.collection = collection
    }

    public subscript(index: Int) -> Element {
        let subrangeStart = collection.index(
            collection.startIndex,
            offsetBy: index * chunkSize,
            limitedBy: collection.endIndex
        )!
        let subrangeEnd =
            collection.index(
                subrangeStart,
                offsetBy: chunkSize,
                limitedBy: collection.endIndex
            ) ?? collection.endIndex
        return collection[subrangeStart..<subrangeEnd]
    }

    public var startIndex: Int { 0 }
    public var endIndex: Int {
        let ratio = Double(collection.count) / Double(chunkSize)
        return Int(ratio.rounded(.up))
    }
    public func index(after i: Int) -> Int { i + 1 }
}

extension Collection {
    public func chunks(ofSize chunkSize: Int) -> Chunks<Self> {
        Chunks(self, chunkSize: chunkSize)
    }
}
